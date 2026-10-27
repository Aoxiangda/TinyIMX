#include "services/message/local/LocalEventRelay.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
using namespace tinyimx::message;
using namespace tinyimx::message::local;
using namespace tinyimx::eventing;
namespace {
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);std::cout<<"PASS "<<text<<'\n';}
template<class F>bool Wait(F&&f,std::chrono::milliseconds limit=std::chrono::seconds(10)){
    const auto end=std::chrono::steady_clock::now()+limit;while(std::chrono::steady_clock::now()<end){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return f();
}
LocalPrivateStoreOptions Options(const std::filesystem::path& root,const std::string& name){
    LocalPrivateStoreOptions o;o.directory=(root/name).string();o.storage_identity="perf107-relay:"+root.filename().string()+':'+name;
    o.first_message_id=7100000000000000ULL;o.message_events_topic="codex_perf107_events_20261020_r1";o.initial_user_ids={500001,500002,500003,500004};o.block_cache_bytes=8*1024*1024;o.exact_user_locking=true;return o;
}
std::string Add(LocalPrivateStore&s,const std::string&cid){const auto r=s.PersistPrivateMessage(500001,500002,cid,1,"event source");Check(r.Accepted()&&r.outcome==PersistPrivateMessageOutcome::kCreated,"event source committed");return"message.created.v1:"+std::to_string(r.message_id);}
class Publisher final:public EventPublisher{
public:
    mutable std::mutex mutex;std::condition_variable cv;
    std::map<std::string,unsigned> attempts;
    std::map<std::string,std::string> accepted;
    std::string transient,permanent,always_retry;
    bool block{false},released{false},entered{false};
    std::filesystem::path journal;
    PublishResult Publish(const PublishMessage&m)override{
        std::unique_lock lock(mutex);const auto attempt=++attempts[m.event_id];
        if(block&&!entered){entered=true;cv.notify_all();cv.wait(lock,[&]{return released;});}
        if(m.event_id==permanent)return{PublishStatus::kPermanentFailure,"","owned permanent result"};
        if(m.event_id==always_retry||(m.event_id==transient&&attempt<3))return{PublishStatus::kRetryableFailure,"","owned transient result"};
        if(!journal.empty()){
            const auto record=m.event_id+'\t'+m.body+'\n';const int fd=::open(journal.c_str(),O_WRONLY|O_CREAT|O_APPEND,0600);if(fd<0)throw std::runtime_error("test journal open");
            std::size_t pos=0;bool good=true;
            while(pos<record.size()){const auto n=::write(fd,record.data()+pos,record.size()-pos);if(n<=0){good=false;break;}pos+=n;}
            good=good&&::fsync(fd)==0;::close(fd);if(!good)throw std::runtime_error("test journal sync");
        }
        accepted[m.event_id]=m.body;return{PublishStatus::kPublished,"test-journal:"+m.event_id,""};
    }
    void Release(){std::lock_guard lock(mutex);released=true;cv.notify_all();}
    bool Entered(){std::lock_guard lock(mutex);return entered;}
    std::size_t Accepted(){std::lock_guard lock(mutex);return accepted.size();}
    void ResetFailures(){std::lock_guard lock(mutex);transient.clear();permanent.clear();always_retry.clear();}
};
LocalEventRelayOptions RelayOptions(){LocalEventRelayOptions o;o.nonblocking_health=true;o.workers=2;o.max_held=8;o.scan_page=16;o.max_attempts=3;o.retry_base=std::chrono::milliseconds(5);o.retry_max=std::chrono::milliseconds(20);o.poll_interval=std::chrono::milliseconds(2);o.sync_interval=std::chrono::milliseconds(20);return o;}
class BatchPublisher final:public EventPublisher{
public:
    enum class Mode { Success, Partial, Missing, Extra, Throw, HoldFirst };
    std::mutex mutex;std::condition_variable cv;
    std::map<std::string,unsigned> attempts;
    std::map<std::string,std::string> accepted;
    std::set<std::string> retry_first;
    Mode mode{Mode::Success};unsigned batch_calls{0};std::size_t largest{0};
    bool hold_second{false},entered{false},released{false};
    std::size_t MaxBatchMessages()const noexcept override{return 32;}
    PublishResult One(const PublishMessage& message){
        const auto attempt=++attempts[message.event_id];
        if(mode==Mode::Partial&&retry_first.contains(message.event_id)&&attempt==1)
            return{PublishStatus::kRetryableFailure,"","owned per-entry failure"};
        accepted[message.event_id]=message.body;
        return{PublishStatus::kPublished,"owned-batch:"+message.event_id,""};
    }
    PublishResult Publish(const PublishMessage& message)override{
        std::lock_guard lock(mutex);return One(message);
    }
    std::vector<PublishResult> PublishBatch(const std::vector<PublishMessage>& messages)override{
        std::unique_lock lock(mutex);++batch_calls;largest=std::max(largest,messages.size());
        if((mode==Mode::HoldFirst&&batch_calls==1)||(hold_second&&batch_calls==2)){
            entered=true;cv.notify_all();
            if(!cv.wait_for(lock,std::chrono::seconds(6),[&]{return released;}))
                throw std::runtime_error("owned bounded batch gate timeout");
        }
        std::vector<PublishResult> result;
        for(const auto& message:messages)result.push_back(One(message));
        if(batch_calls==1){
            if(mode==Mode::Missing)result.pop_back();
            if(mode==Mode::Extra)result.push_back({PublishStatus::kPublished,"foreign",""});
            if(mode==Mode::Throw)throw std::runtime_error("owned ambiguous whole-batch exception");
        }
        return result;
    }
    void Release(){std::lock_guard lock(mutex);released=true;cv.notify_all();}
    bool Entered(){std::lock_guard lock(mutex);return entered;}
};
struct BatchGateRelease final{
    BatchPublisher& publisher;
    ~BatchGateRelease(){publisher.Release();}
};
LocalEventRelayOptions BatchRelayOptions(){
    auto options=RelayOptions();options.workers=1;options.max_held=64;options.scan_page=64;
    options.progress_batch_size=16;options.retry_base=std::chrono::milliseconds(30);
    options.retry_max=std::chrono::milliseconds(30);return options;
}
void BatchContracts(const std::filesystem::path& root){
    {
        LocalPrivateStore store(Options(root,"wire-batch-partial"));BatchPublisher publisher;
        publisher.mode=BatchPublisher::Mode::Partial;std::vector<std::string> ids;
        for(unsigned i=0;i<32;++i){ids.push_back(Add(store,"wire-partial-"+std::to_string(i)));if(i%2)publisher.retry_first.insert(ids.back());}
        LocalEventRelay relay(&store,&publisher,BatchRelayOptions());
        Check(relay.Start()&&Wait([&]{return store.Stats().pending_events==0;})&&relay.Shutdown(),"ready-only batches drain partial results through durable per-event progress");
        const auto stats=relay.Stats();std::lock_guard lock(publisher.mutex);
        Check(publisher.largest==16&&publisher.batch_calls>=2&&publisher.accepted.size()==32,"existing ready/progress limit forms real16-entry publisher calls");
        for(unsigned i=0;i<32;++i)Check(publisher.attempts.at(ids[i])==(i%2?2U:1U),"only explicitly failed batch entry is retried");
        Check(stats.published==32&&stats.retryable_failures==16&&stats.quarantined==0&&stats.held==0&&stats.active==0&&stats.held_peak<=64,"partial batch preserves bounded ownership and exact result counts");
        Check(store.ReadUnreadSnapshot(500002,500001).total_unread==32,"batch publication does not duplicate message or unread truth");
    }
    for(const auto mode:{BatchPublisher::Mode::Missing,BatchPublisher::Mode::Extra,BatchPublisher::Mode::Throw}){
        const std::string name=mode==BatchPublisher::Mode::Missing?"missing":mode==BatchPublisher::Mode::Extra?"extra":"throw";
        LocalPrivateStore store(Options(root,"wire-batch-"+name));BatchPublisher publisher;
        publisher.mode=mode;publisher.hold_second=true;std::vector<std::string> ids;
        for(unsigned i=0;i<16;++i)ids.push_back(Add(store,"wire-"+name+'-'+std::to_string(i)));
        LocalEventRelay relay(&store,&publisher,BatchRelayOptions());BatchGateRelease cleanup{publisher};
        Check(relay.Start()&&Wait([&]{return publisher.Entered();}),"malformed or thrown batch reaches bounded retry gate");
        const bool no_false_progress=relay.Stats().published==0&&store.Stats().pending_events==16;
        publisher.Release();
        Check(no_false_progress&&Wait([&]{return store.Stats().pending_events==0;})&&relay.Shutdown(),"unknown batch result confirms nothing and all original events recover");
        std::lock_guard lock(publisher.mutex);
        Check(publisher.accepted.size()==16&&publisher.attempts.size()==16&&std::all_of(ids.begin(),ids.end(),[&](const auto& id){return publisher.attempts.at(id)==2;}),"ambiguous batch recovery keeps stable event IDs without losing originals");
        Check(store.ReadUnreadSnapshot(500002,500001).total_unread==16&&relay.Stats().quarantined==0,"ambiguous batch retry keeps business truth exactly once");
    }
    {
        LocalPrivateStore store(Options(root,"wire-batch-shutdown"));BatchPublisher publisher;publisher.mode=BatchPublisher::Mode::HoldFirst;
        for(unsigned i=0;i<32;++i)(void)Add(store,"wire-drain-"+std::to_string(i));
        auto options=BatchRelayOptions();LocalEventRelay relay(&store,&publisher,options);BatchGateRelease cleanup{publisher};
        Check(relay.Start()&&Wait([&]{return publisher.Entered();}),"shutdown test holds one actual16-entry batch");
        auto shutdown=std::async(std::launch::async,[&]{return relay.Shutdown();});
        const bool fenced=Wait([&]{return relay.Stats().stopping;})&&!relay.Healthy();
        const bool waiting=shutdown.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout;
        publisher.Release();const bool clean=shutdown.get();
        Check(fenced&&waiting&&clean&&relay.Stats().published==16&&store.Stats().pending_events==16,"shutdown drains submitted batch and leaves unattempted16 events durable");
        LocalEventRelay next(&store,&publisher,options);
        Check(next.Start()&&Wait([&]{return store.Stats().pending_events==0;})&&next.Shutdown(),"next relay owner resumes only remaining durable batch");
        std::lock_guard lock(publisher.mutex);
        Check(publisher.attempts.size()==32&&std::all_of(publisher.attempts.begin(),publisher.attempts.end(),[](const auto& value){return value.second==1;}),"normal batch shutdown introduces no duplicate publication or lost event");
        Check(store.ReadUnreadSnapshot(500002,500001).total_unread==32,"shutdown and resume preserve all32 original messages");
    }
}
void Contracts(const std::filesystem::path& root){
    BatchContracts(root);
    for(const bool nonblocking:{false,true}){
        LocalPrivateStore s(Options(root,nonblocking?"health-on":"health-off"));Publisher p;
        auto options=RelayOptions();options.nonblocking_health=nonblocking;
        std::atomic<bool> entered{false};std::promise<void> release;auto released=release.get_future().share();
        options.before_pending_scan=[&]{if(!entered.exchange(true))released.wait();};
        LocalEventRelay relay(&s,&p,options);
        Check(!relay.Healthy(),"health is false before publisher start");
        Check(relay.Start(),"health contract publisher starts");
        const bool scanning=Wait([&]{return entered.load();});
        auto health=std::async(std::launch::async,[&]{return relay.Healthy();});
        const auto state=health.wait_for(std::chrono::milliseconds(100));
        release.set_value();const bool healthy=health.get();
        Check(scanning&&healthy&&(nonblocking?(state==std::future_status::ready):(state==std::future_status::timeout)),
              "health candidate avoids scan lock; baseline demonstrably waits for same lock");
        Check(relay.Shutdown()&&!relay.Healthy()&&relay.Shutdown()&&!relay.Start(),
              "health remains false after idempotent shutdown and rejected restart");
    }
    for(const bool diagnostic_failure:{false,true}){
        LocalPrivateStore s(Options(root,diagnostic_failure?"health-fatal-diagnostic-failure":"health-fatal"));Publisher p;auto options=RelayOptions();
        std::atomic<bool> notified{false},observed_unhealthy{false};LocalEventRelay* ptr=nullptr;
        options.before_pending_scan=[]{throw std::runtime_error("owned scan failure");};
        if(diagnostic_failure)options.before_error_assignment=[]{throw std::bad_alloc();};
        options.on_fatal=[&]{observed_unhealthy=!ptr->Healthy();notified=true;};
        LocalEventRelay relay(&s,&p,options);ptr=&relay;
        Check(relay.Start()&&Wait([&]{return notified.load();})&&observed_unhealthy&&!relay.Healthy(),
              "fatal callback already observes health fence without waiting for stats lock");
        Check(!relay.Shutdown()&&relay.Stats().failed&&!relay.Healthy()&&
              (!diagnostic_failure||relay.Stats().error.empty()),
              "fatal shutdown preserves health fence and callback even if diagnostic allocation fails");
    }
    {
        LocalPrivateStore s(Options(root,"basic"));Publisher p;std::vector<std::string> ids;
        for(unsigned i=0;i<64;++i)ids.push_back(Add(s,"relay-"+std::to_string(i)));
        p.transient=ids[0];p.permanent=ids[1];p.always_retry=ids[2];auto options=RelayOptions();LocalEventRelay relay(&s,&p,options),second(&s,&p,options);
        Check(relay.Start()&&!second.Start(),"one publisher owner per store");
        const bool drained=Wait([&]{return s.Stats().pending_events==0;});
        if(!drained){const auto debug=relay.Stats();std::cerr<<"relay pending="<<s.Stats().pending_events<<" published="<<debug.published<<" retry="<<debug.retryable_failures<<" quarantined="<<debug.quarantined<<" error="<<debug.error<<'\n';}
        Check(drained,"bounded workers progress beyond retries and permanent event");
        Check(Wait([&]{return relay.Stats().progress_syncs>0;}),"publication progress has periodic explicit WAL sync");
        Check(relay.Shutdown(),"successful shutdown joins and syncs publication progress");
        const auto stats=relay.Stats();const auto failed=s.ListFailedEvents(256);
        Check(stats.published==62&&stats.retryable_failures==5&&stats.permanent_failures==1&&stats.quarantined==2&&!stats.failed,"retry limit and permanent outcomes retained exactly");
        Check(stats.held_peak<=8&&stats.held==0&&stats.queued==0&&stats.active==0&&p.Accepted()==62,"bounded held work fully releases after shutdown");
        Check(failed.Succeeded()&&failed.events.size()==2&&s.GetPrivateMessage(7100000000000000ULL).Found()&&s.ReadUnreadSnapshot(500002,500001).total_unread==64,"quarantine retains original events and does not change message/unread truth");
        std::set<std::string> failures;for(const auto& f:failed.events){failures.insert(f.event.event_id);Check(!f.reason.empty()&&!f.event.body.empty(),"quarantine preserves original envelope and reason");}
        Check(failures==std::set<std::string>{ids[1],ids[2]},"only failed event identities quarantined");
        p.ResetFailures();Check(s.RequeueFailedEvents({ids[1],ids[2],ids[1]}).affected_rows==2,"explicit requeue preserves identities and deduplicates input");
        Check(second.Start()&&relay.Shutdown(),"new owner starts after prior owner release; old shutdown is idempotent");
        LocalEventRelay third(&s,&p,options);Check(!third.Start(),"old owner cannot release the new publisher lease");
        Check(Wait([&]{return s.Stats().pending_events==0;})&&second.Shutdown()&&p.Accepted()==64&&s.ListFailedEvents(256).events.empty(),"requeued events reach success without losing original records");
    }
    {
        auto o=Options(root,"quota");o.max_pending_events=1;LocalPrivateStore s(o);
        const auto a=Add(s,"qa");Check(s.MarkEventFailed(a,"owned_failure").affected_rows==1,"failed transition is durable");const auto b=Add(s,"qb");
        Check(!s.RequeueFailedEvents({a}).Succeeded()&&s.ListFailedEvents(256).events.size()==1&&s.Stats().pending_events==1,"requeue quota failure does not partially move failed event");
        Check(s.MarkEventsPublished({b}).affected_rows==1&&s.RequeueFailedEvents({a}).affected_rows==1&&s.ListFailedEvents(256).events.empty(),"requeue succeeds after capacity is available");
        const auto actors=s.ListMessageActors();Check(actors.Succeeded()&&actors.has_messages&&actors.actors==std::vector<std::uint64_t>{500001,500002},"durable actor index matches both message participants");
    }
    {
        LocalPrivateStore s(Options(root,"drain"));Publisher p;p.block=true;auto options=RelayOptions();options.workers=1;options.max_held=4;
        for(unsigned i=0;i<20;++i)(void)Add(s,"drain-"+std::to_string(i));
        LocalEventRelay relay(&s,&p,options);Check(relay.Start(),"drain publisher starts");
        const bool entered=Wait([&]{return p.Entered();});if(!entered){p.Release();relay.Shutdown();Check(false,"blocked publisher entered");}
        auto stop=std::async(std::launch::async,[&]{return relay.Shutdown();});const bool stopping=Wait([&]{return relay.Stats().stopping;});
        const bool waiting=stop.wait_for(std::chrono::milliseconds(20))==std::future_status::timeout;
        const bool health_fenced=!relay.Healthy();
        p.Release();const bool stopped=stop.get();
        Check(stopping&&waiting&&stopped&&health_fenced&&s.Stats().pending_events==19&&p.Accepted()==1,"stop fences health, waits for current publication and leaves unattempted queue durable");
        Publisher recovered;LocalEventRelay again(&s,&recovered,options);Check(again.Start()&&Wait([&]{return s.Stats().pending_events==0;})&&again.Shutdown()&&recovered.Accepted()==19,"next owner resumes exactly the remaining queued records");
    }
    {
        auto o=Options(root,"admission");std::atomic<bool> admit{false};o.admit_events=[&]{return admit.load();};
        LocalPrivateStore s(o);Check(!s.PersistPrivateMessage(500001,500002,"new",1,"b").Accepted()&&s.Stats().pending_events==0,"fenced admission rejects new event without partial state");
        admit=true;const auto first=s.PersistPrivateMessage(500001,500002,"new",1,"b");admit=false;
        Check(first.Accepted()&&s.PersistPrivateMessage(500001,500002,"new",1,"b").outcome==PersistPrivateMessageOutcome::kReused,"durable CID retry remains available across event admission fence");
        Check(!s.MarkDialogRead(500002,500001).Succeeded()&&s.GetPrivateMessage(first.message_id).record.delivery_state==MessageDeliveryState::kPending,"fenced read mutation creates no unpublishable event");
    }
}
}
int main(int argc,char**argv){
    try{
        if(argc!=3)return 2;const std::filesystem::path root=argv[1];const std::string mode=argv[2];
        Check(root.is_absolute()&&(root.string().starts_with("/home/jackson7/projects/tinyimx-interview-20261017/perf105-")||root.string().starts_with("/home/jackson7/projects/tinyimx-interview-20261017/perf107-")||root.string().starts_with("/mnt/tinyimx-benchmark/perf943-contracts-"))&&std::filesystem::canonical(root)==root,"owned physical event-relay fixture");
        if(mode=="contracts"){Contracts(root);std::cout<<"LOCAL_EVENT_RELAY_CONTRACTS_PASS\n";return 0;}
        const bool crash=mode.starts_with("crash-"),verify=mode.starts_with("verify-");
        const bool quarantine=mode.ends_with("after-quarantine");
        if((!crash&&!verify)||(!quarantine&&!mode.ends_with("after-publish")))return 2;
        auto options=Options(root,"fault");LocalPrivateStore s(options);Publisher p;p.journal=root/"publisher-contract-journal.tsv";
        auto relay_options=RelayOptions();relay_options.workers=1;relay_options.max_held=1;
        if(crash){
            const auto id=Add(s,"fault-cid");
            if(quarantine)p.permanent=id;
            else relay_options.after_success_before_progress=[&](const auto&e){std::cout<<"OWNED_LOCAL_EVENT_CRASH_READY "<<mode<<' '<<e.event_id<<std::endl;std::raise(SIGSTOP);};
            LocalEventRelay relay(&s,&p,relay_options);Check(relay.Start(),"fault publisher starts");
            if(quarantine){Check(Wait([&]{return s.ListFailedEvents(256).events.size()==1;}),"quarantine sync completed");std::cout<<"OWNED_LOCAL_EVENT_CRASH_READY "<<mode<<' '<<id<<std::endl;std::raise(SIGSTOP);}
            else{std::this_thread::sleep_for(std::chrono::seconds(20));}
            return 3;
        }
        const auto found=s.FindPrivateMessageByClientMessageId(500001,"fault-cid");Check(found.Found(),"original message survives publisher process death");
        if(quarantine){const auto failed=s.ListFailedEvents(256);Check(failed.Succeeded()&&failed.events.size()==1&&s.Stats().pending_events==0,"failed envelope survived before progress cleanup");Check(s.RequeueFailedEvents({failed.events[0].event.event_id}).affected_rows==1,"recovered failed envelope explicitly requeued");}
        else Check(s.Stats().pending_events==1,"broker-success-before-marker death keeps original event pending");
        LocalEventRelay relay(&s,&p,relay_options);Check(relay.Start()&&Wait([&]{return s.Stats().pending_events==0;})&&relay.Shutdown(),"recovered event reaches publication progress sync");
        std::ifstream input(p.journal);std::vector<std::string> records;for(std::string line;std::getline(input,line);)records.push_back(line);
        Check(records.size()==(quarantine?1:2)&&std::all_of(records.begin(),records.end(),[&](const auto&v){return v==records.front();}),"publisher contract journal proves stable duplicate envelope or one recovered failed send");
        Check(s.PersistPrivateMessage(500001,500002,"fault-cid",1,"event source").outcome==PersistPrivateMessageOutcome::kReused&&s.CountPending(500002).count==1,"publication recovery never duplicates message or receiver state");
        std::cout<<"LOCAL_EVENT_RELAY_RECOVERY_PASS "<<mode<<'\n';return 0;
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
