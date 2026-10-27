#include "services/message/local/LocalEventRelay.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <sys/resource.h>

using namespace tinyimx::message;
using namespace tinyimx::message::local;
using namespace tinyimx::eventing;
using namespace std::chrono_literals;
namespace {
unsigned checks=0;
void Check(bool ok,const char* label){if(!ok)throw std::runtime_error(label);++checks;}
LocalPrivateStoreOptions Options(const std::filesystem::path& root,const std::string& name){
    LocalPrivateStoreOptions o;o.directory=(root/name).string();
    o.storage_identity="owned-perf894-"+root.filename().string()+":"+name;
    o.first_message_id=7894000000000000ULL;o.message_events_topic="codex_perf894_dirty_progress";
    o.initial_user_ids={500001,500002,500003};o.block_cache_bytes=8*1024*1024;o.exact_user_locking=true;return o;
}
std::string Persist(LocalPrivateStore& s,const std::string& cid){
    const auto r=s.PersistPrivateMessage(500001,500002,cid,1,"durable "+cid);
    Check(r.Accepted(),"success follows durable private commit");
    return "message.created.v1:"+std::to_string(r.message_id);
}
void SyncContracts(const std::filesystem::path& root){
    const auto o=Options(root,"sync-contracts");
    {
        LocalPrivateStore s(o);const auto start=s.Stats().engine_wal_syncs;
        auto r=s.SyncPublicationProgressIfDirty();
        Check(r.Succeeded()&&r.affected_rows==1&&s.Stats().engine_wal_syncs==start+1,"first dirty sync always flushes recovered owner");
        const auto idle=s.Stats().engine_wal_syncs;
        for(unsigned i=0;i<50;++i){r=s.SyncPublicationProgressIfDirty();Check(r.Succeeded()&&r.affected_rows==0,"clean progress skip succeeds");}
        Check(s.Stats().engine_wal_syncs==idle,"clean calls do not issue engine WAL sync");
        const auto event=Persist(s,"first");const auto committed=s.Stats().engine_wal_syncs;
        Check(committed>idle,"message durability still performs sync");
        Check(s.SyncPublicationProgressIfDirty().affected_rows==0,"message commit alone creates no unsynced publication marker");
        Check(s.MarkEventsPublished({event}).affected_rows==1,"only existing publication marker removed");
        Check(s.Stats().engine_wal_syncs==committed,"marker remains originally asynchronous");
        r=s.SyncPublicationProgressIfDirty();
        Check(r.Succeeded()&&r.affected_rows==1&&s.Stats().engine_wal_syncs==committed+1,"completed marker forces next sync");
        const auto n=s.Stats().engine_wal_syncs;
        Check(s.MarkEventsPublished({event,event}).affected_rows==0,"duplicate publication result is idempotent");
        Check(s.SyncPublicationProgressIfDirty().affected_rows==0&&s.Stats().engine_wal_syncs==n,"no-op marker does not dirty progress");
        const auto m=s.FindPrivateMessageByClientMessageId(500001,"first");
        Check(m.Succeeded()&&s.ConfirmReceiver(m.record.message_id,500003).status==MessageApplicationStatus::kPermissionDenied,"foreign confirmation still rejected");
        Check(s.ConfirmReceiver(m.record.message_id,500002).Succeeded(),"receiver confirmation retained");
        Check(s.GetPrivateMessage(m.record.message_id).record.delivery_state==MessageDeliveryState::kReceiverConfirmed,"confirmation is not skipped");
        Check(s.MarkDialogRead(500002,500001).Succeeded()&&s.ReadUnreadSnapshot(500002,500001).total_unread==0,"read state and unread snapshot retained");
        const auto before_force=s.Stats().engine_wal_syncs;
        Check(s.SyncPublicationProgress().Succeeded()&&s.Stats().engine_wal_syncs==before_force+1,"legacy unconditional sync remains unconditional");
        Check(s.Shutdown(),"shutdown retains forced final engine sync");
        Check(!s.SyncPublicationProgressIfDirty().Succeeded(),"closed owner cannot report accepted sync");
    }
    LocalPrivateStore reopened(o);
    const auto m=reopened.FindPrivateMessageByClientMessageId(500001,"first");
    Check(m.Succeeded()&&m.record.delivery_state==MessageDeliveryState::kRead,"message confirmation and read survive reopened owner");
    Check(reopened.PersistPrivateMessage(500001,500002,"first",1,"durable first").outcome==PersistPrivateMessageOutcome::kReused,"CID remains stable after restart");
    Check(reopened.PersistPrivateMessage(500001,500002,"first",1,"different").outcome==PersistPrivateMessageOutcome::kIdempotencyConflict,"CID conflict remains rejected");
    Check(reopened.Shutdown(),"reopened owner closes");
}
void ConcurrentCheckpoint(const std::filesystem::path& root){
    std::mutex mutex;std::condition_variable cv;bool armed=false,entered=false,released=false;
    LocalPrivateStore s(Options(root,"generation-race"),[&](auto point,auto){
        if(point!=LocalPrivateStore::CommitPoint::kAfterPublicationSyncBeforeCheckpoint)return;
        std::unique_lock lock(mutex);if(!armed)return;entered=true;cv.notify_all();
        if(!cv.wait_for(lock,5s,[&]{return released;}))throw std::runtime_error("test gate timeout");
    });
    Check(s.SyncPublicationProgressIfDirty().Succeeded(),"race owner initialized");
    const auto e1=Persist(s,"race1"),e2=Persist(s,"race2");
    Check(s.MarkEventsPublished({e1}).affected_rows==1,"first generation published");
    {std::lock_guard lock(mutex);armed=true;}
    auto flushing=std::async(std::launch::async,[&]{return s.SyncPublicationProgressIfDirty();});
    {std::unique_lock lock(mutex);Check(cv.wait_for(lock,5s,[&]{return entered;}),"paused after real WAL sync before checkpoint");}
    Check(s.MarkEventsPublished({e2}).affected_rows==1,"concurrent newer publication completes after flush");
    {std::lock_guard lock(mutex);released=true;}cv.notify_all();
    Check(flushing.get().Succeeded(),"captured generation sync returns");
    {std::lock_guard lock(mutex);armed=false;}
    const auto n=s.Stats().engine_wal_syncs;
    Check(s.SyncPublicationProgressIfDirty().affected_rows==1&&s.Stats().engine_wal_syncs==n+1,"concurrent newer marker remains dirty for next sync");
    Check(s.SyncPublicationProgressIfDirty().affected_rows==0,"following clean call skips only after new generation synced");
    Check(s.Shutdown(),"generation race shuts down");
}
void ConcurrentShutdown(const std::filesystem::path& root){
    std::mutex mutex;std::condition_variable cv;bool entered=false,released=false;
    LocalPrivateStore s(Options(root,"shutdown-race"),[&](auto point,auto){
        if(point!=LocalPrivateStore::CommitPoint::kAfterPublicationSyncBeforeCheckpoint)return;
        std::unique_lock lock(mutex);entered=true;cv.notify_all();
        if(!cv.wait_for(lock,5s,[&]{return released;}))throw std::runtime_error("shutdown test gate timeout");
    });
    auto syncing=std::async(std::launch::async,[&]{return s.SyncPublicationProgressIfDirty();});
    {std::unique_lock lock(mutex);Check(cv.wait_for(lock,5s,[&]{return entered;}),"sync holds owner lifetime during checkpoint");}
    std::promise<void> attempted;auto signal=attempted.get_future();
    auto stopping=std::async(std::launch::async,[&]{attempted.set_value();return s.Shutdown();});signal.wait();
    Check(stopping.wait_for(50ms)==std::future_status::timeout,"shutdown waits for in-flight sync lifetime");
    {std::lock_guard lock(mutex);released=true;}cv.notify_all();
    Check(syncing.get().Succeeded()&&stopping.get(),"sync completes before successful owner shutdown");
}
class Publisher final:public EventPublisher{
public:
    std::atomic<unsigned> calls{0};
    PublishResult Publish(const PublishMessage&)override{++calls;return{PublishStatus::kPublished,"owned-fake-broker", ""};}
};
double CpuMs(){rusage u{};getrusage(RUSAGE_SELF,&u);return(u.ru_utime.tv_sec+u.ru_stime.tv_sec)*1000.0+(u.ru_utime.tv_usec+u.ru_stime.tv_usec)/1000.0;}
void RelayAblation(const std::filesystem::path& root){
    LocalPrivateStore store(Options(root,"relay"));Publisher publisher;
    LocalEventRelayOptions ro;ro.workers=2;ro.max_held=8;ro.scan_page=8;ro.poll_interval=10ms;ro.sync_interval=100ms;
    LocalEventRelay relay(&store,&publisher,ro);
    Check(relay.Start(),"real relay starts");const auto start=store.Stats().engine_wal_syncs;const auto cpu=CpuMs();
    std::this_thread::sleep_for(550ms);
    const auto ticks=store.Stats().engine_wal_syncs-start;const auto cpu_delta=CpuMs()-cpu;
    const char* flag=std::getenv("TINYIMX_NATIVE_PUBLICATION_DIRTY_SYNC_ENABLE");const bool on=flag&&std::string(flag)=="1";
    Check(on?ticks==1:ticks>=3,"idle real relay only suppresses clean periodic sync when explicitly enabled");
    for(unsigned i=0;i<64;++i)Persist(store,"relay-"+std::to_string(i));
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(store.Stats().pending_events){Check(std::chrono::steady_clock::now()<deadline,"real relay drain deadline");std::this_thread::sleep_for(2ms);}
    Check(relay.Shutdown(),"relay joins workers and forces final progress sync");
    Check(publisher.calls==64&&relay.Stats().published==64&&store.Stats().pending_events==0,"all64 real local events published once without suppression");
    Check(store.ListHistory(500002,500001,0,100).records.size()==64,"all64 durable messages retained");
    std::cout<<nlohmann::json{{"dirty_sync",on},{"idle_ms",550},{"idle_wal_sync_ticker_delta",ticks},{"idle_process_cpu_ms",cpu_delta},{"published",publisher.calls.load()},{"pending",store.Stats().pending_events},{"relay_progress_syncs",relay.Stats().progress_syncs},{"capacity_or_full_business_result",false}}.dump()<<'\n';
    Check(store.Shutdown(),"relay store closes");
}
void Crash(const std::filesystem::path& root,bool after){
    LocalPrivateStore s(Options(root,after?"crash-after":"crash-before"));
    const auto event=Persist(s,"crash-cid");Check(s.MarkEventsPublished({event}).affected_rows==1,"crash marker written");
    if(after)Check(s.SyncPublicationProgressIfDirty().Succeeded(),"crash after successful dirty sync");
    std::cout<<"DIRTY_PROGRESS_REAL_CRASH_READY"<<std::endl;std::raise(SIGSTOP);
    throw std::runtime_error("crash child must be killed by owned parent");
}
void Recover(const std::filesystem::path& root,bool after){
    LocalPrivateStore s(Options(root,after?"crash-after":"crash-before"));
    const auto m=s.FindPrivateMessageByClientMessageId(500001,"crash-cid");
    Check(m.Succeeded()&&m.record.content=="durable crash-cid","ACKed message survives process death");
    Check(s.PersistPrivateMessage(500001,500002,"crash-cid",1,"durable crash-cid").outcome==PersistPrivateMessageOutcome::kReused,"crash retry preserves stable business message");
    const auto p=s.ListPendingEvents(8);Check(p.Succeeded()&&p.events.size()<=1,"unsynced progress permits at most same event replay");
    if(after)Check(p.events.empty(),"synced publication marker is recovered");
    if(!p.events.empty())Check(p.events[0].event_id=="message.created.v1:"+std::to_string(m.record.message_id),"replay has exact original event identity");
    Check(s.SyncPublicationProgressIfDirty().Succeeded()&&s.Shutdown(),"recovered owner syncs and closes");
}
}
int main(int argc,char**argv)try{
    if(argc!=3)return 2;const std::filesystem::path root=argv[1];const std::string mode=argv[2];
    Check(root.is_absolute()&&root.string().starts_with("/mnt/tinyimx-benchmark/perf894-")&&std::filesystem::canonical(root)==root,"exact owned canonical test path");
    if(mode=="contracts"){SyncContracts(root);ConcurrentCheckpoint(root);ConcurrentShutdown(root);}
    else if(mode=="relay")RelayAblation(root);
    else if(mode=="crash-before"||mode=="crash-after")Crash(root,mode=="crash-after");
    else if(mode=="recover-before"||mode=="recover-after")Recover(root,mode=="recover-after");
    else return 2;
    std::cout<<"NATIVE_DIRTY_SYNC_QUALIFICATION_PASS checks="<<checks<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<"NATIVE_DIRTY_SYNC_QUALIFICATION_FAIL "<<e.what()<<'\n';return 1;}
