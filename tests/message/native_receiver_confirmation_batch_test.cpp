#include "services/message/local/LocalMessageRepositoryAdapter.h"
#include "tests/message/ReadyBatchNoopRepository.h"
#include <rocksdb/db.h>
#include <rocksdb/write_batch.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <future>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <thread>

using namespace tinyimx::message;
using namespace tinyimx::message::local;
using namespace std::chrono_literals;
namespace {
void Check(bool value,const char* label){if(!value)throw std::runtime_error(label);std::cout<<"PASS "<<label<<'\n';}
LocalPrivateStoreOptions Options(const std::filesystem::path& root,const std::string& name,bool exact=true){
    LocalPrivateStoreOptions o;o.directory=(root/name).string();o.storage_identity="owned-native-confirm-r854:"+root.filename().string()+':'+name;
    o.first_message_id=7085400000000000ULL;o.message_events_topic="codex_perf854_confirm_events";
    o.initial_user_ids={1,2,3,4,5,6};o.block_cache_bytes=1024*1024;o.exact_user_locking=exact;return o;
}
std::uint64_t New(LocalPrivateStore& store,int index,std::uint64_t from=1,std::uint64_t to=2){
    auto out=store.PersistPrivateMessage(from,to,"cid-"+std::to_string(index),1,"owned fixture body");
    if(!out.Accepted())throw std::runtime_error("fixture persistence failed: "+out.message);return out.message_id;
}
void Mixed(const std::filesystem::path& root,bool exact){
    auto options=Options(root,exact?"mixed-exact":"mixed-striped",exact);std::uint64_t a,b,r;
    {
        LocalPrivateStore s(options);a=New(s,0);b=New(s,1,3,4);r=New(s,2,5,6);
        Check(s.MarkDialogRead(6,5).Succeeded(),"already-read fixture");
        auto out=s.ConfirmReceiverRequests({{a,2},{a,3},{a,2},{b,4},{0,2},{1,2},{r,6},{r,2},{b,0},{b,999}});
        Check(out.size()==10&&out[0].Succeeded()&&out[0].affected_rows==1&&out[2].Succeeded()&&out[2].affected_rows==0,
            "duplicate callers share exactly one pending transition");
        Check(out[1].status==MessageApplicationStatus::kPermissionDenied&&out[7].status==MessageApplicationStatus::kPermissionDenied,
            "foreign owners denied before and after a legitimate batch transition");
        Check(out[3].Succeeded()&&out[3].affected_rows==1&&out[4].status==MessageApplicationStatus::kInvalidArgument&&
            out[5].status==MessageApplicationStatus::kNotFound&&out[6].Succeeded()&&out[6].affected_rows==0&&
            out[8].status==MessageApplicationStatus::kInvalidArgument&&out[9].status==MessageApplicationStatus::kPermissionDenied,
            "each request retains its own invalid, missing, read and actor decision");
        Check(s.CountPending(2).count==0&&s.CountPending(4).count==0&&s.ReadUserUnreadSnapshot(2).total_unread==1&&
            s.ReadUserUnreadSnapshot(4).total_unread==1&&s.ReadUserUnreadSnapshot(6).total_unread==0,
            "confirmation changes pending only and never regresses read or unread");
        Check(s.Stats().confirmation_commits==1,"two distinct receivers use one synced logical confirmation commit");
        Check(s.ConfirmReceiverBatch({a,a}).status==MessageApplicationStatus::kInvalidArgument,
            "public all-or-nothing duplicate-MID batch contract remains unchanged");
        std::vector<OwnedReceiverConfirmation> too_many(65,{a,2});auto rejected=s.ConfirmReceiverRequests(too_many);
        Check(rejected.size()==65&&rejected.front().status==MessageApplicationStatus::kInvalidArgument&&
            s.ConfirmReceiverRequests({}).empty(),"bounded input and empty batch");
        Check(s.Shutdown(),"clean mixed shutdown");
    }
    LocalPrivateStore recovered(options);
    Check(recovered.GetPrivateMessage(a).record.delivery_state==MessageDeliveryState::kReceiverConfirmed&&
        recovered.GetPrivateMessage(b).record.delivery_state==MessageDeliveryState::kReceiverConfirmed&&
        recovered.GetPrivateMessage(r).record.delivery_state==MessageDeliveryState::kRead&&recovered.CountPending(2).count==0,
        "durable confirmation and monotonic read recover exactly");
}
void WriteFailure(const std::filesystem::path& root){
    LocalPrivateStore s(Options(root,"failure"),[](auto point,auto){
        if(point==LocalPrivateStore::CommitPoint::kBeforeConfirmationBatchWrite)throw std::runtime_error("injected before-write failure");
    });auto a=New(s,0);auto out=s.ConfirmReceiverRequests({{a,2},{a,2},{a,3},{1,2}});
    Check(!out[0].Succeeded()&&!out[1].Succeeded()&&out[0].affected_rows==0&&out[1].affected_rows==0&&
        out[2].status==MessageApplicationStatus::kPermissionDenied&&out[3].status==MessageApplicationStatus::kNotFound,
        "failed write never exposes provisional success including duplicate waiter");
    Check(s.CountPending(2).count==1&&s.GetPrivateMessage(a).record.delivery_state==MessageDeliveryState::kPending&&
        s.Stats().confirmation_commits==0,"pre-write failure leaves records and exact counters untouched");
}
void InvalidNeighbor(const std::filesystem::path& root){
    auto options=Options(root,"invalid-neighbor");std::uint64_t good,bad,failed;
    {LocalPrivateStore s(options);good=New(s,0);bad=New(s,1);failed=New(s,2);Check(s.Shutdown(),"close before owned corruption fixture");}
    const auto hex=[](std::uint64_t n){std::ostringstream out;out<<std::hex<<std::setfill('0')<<std::setw(16)<<n;return out.str();};
    {
        std::unique_ptr<rocksdb::DB> db;rocksdb::Options engine;engine.create_if_missing=false;
        Check(rocksdb::DB::Open(engine,options.directory,&db).ok(),"open only owned test database for malformed-state fixtures");
        rocksdb::WriteOptions write;write.sync=true;
        std::string value;Check(db->Get({},"M/"+hex(failed),&value).ok(),"read owned failed-state fixture");
        auto record=nlohmann::json::parse(value);record["state"]=static_cast<unsigned>(MessageDeliveryState::kFailed);
        rocksdb::WriteBatch batch;batch.Put("M/"+hex(failed),record.dump());batch.Delete("P/"+hex(2)+'/'+hex(failed));
        std::string count(8,'\0');count.back()=2;batch.Put("Q/"+hex(2),count);batch.Put("M/"+hex(bad),"{malformed-json");
        Check(db->Write(write,&batch).ok()&&db->Close().ok(),"durably seed isolated invalid and failed neighbors");
    }
    LocalPrivateStore s(options);auto out=s.ConfirmReceiverRequests({{good,2},{bad,2},{failed,2},{good,2}});
    Check(out[0].Succeeded()&&out[0].affected_rows==1&&out[3].Succeeded()&&out[3].affected_rows==0&&
        out[1].status==MessageApplicationStatus::kInvalidRecord&&out[2].status==MessageApplicationStatus::kFailedPrecondition,
        "malformed and failed neighboring requests retain independent errors without aborting valid confirmation");
    Check(s.CountPending(2).count==1&&s.Stats().confirmation_commits==1,
        "only valid unique confirmation changes pending counter in mixed invalid batch");
}
void Ready(const std::filesystem::path& root,bool close){
    std::promise<void> entered,release;auto released=release.get_future().share();std::atomic<int> hooks{0};
    auto options=Options(root,close?"close":"ready");
    LocalPrivateStore s(options,[&](auto point,auto){if(point==LocalPrivateStore::CommitPoint::kBeforeConfirmationBatchWrite&&hooks.fetch_add(1)==0){entered.set_value();released.wait();}});
    std::vector<std::uint64_t> mids;for(int i=0;i<17;++i)mids.push_back(New(s,i));
    tinyimx::tests::ReadyBatchNoopRepository groups;LocalMessageRepositoryAdapter adapter(&s,&groups,true);
    auto first=std::async(std::launch::async,[&]{return *adapter.TryConfirmReceiverOwnedIfPending(mids[0],2);});
    if(entered.get_future().wait_for(5s)!=std::future_status::ready){release.set_value();throw std::runtime_error("first synced batch never entered");}
    std::vector<std::future<MessageRepositoryMutationResult>> waiters;
    for(std::size_t i=1;i<mids.size();++i)waiters.emplace_back(std::async(std::launch::async,[&,i]{return *adapter.TryConfirmReceiverOwnedIfPending(mids[i],2);}));
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(adapter.ReceiverConfirmationQueuedForTest()!=16&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
    const bool all_queued=adapter.ReceiverConfirmationQueuedForTest()==16;
    std::future<bool> shutdown;if(close)shutdown=std::async(std::launch::async,[&]{return s.Shutdown();});
    const bool no_early_reply=first.wait_for(0s)==std::future_status::timeout;
    release.set_value();auto first_result=first.get();std::size_t successes=first_result.Succeeded();
    for(auto& waiter:waiters){Check(waiter.wait_for(5s)==std::future_status::ready,"bounded waiter completes");auto result=waiter.get();
        Check(result.Succeeded()||result.status==MessageApplicationStatus::kStorageError,"stop returns durable success or closed failure");successes+=result.Succeeded();}
    Check(all_queued&&no_early_reply,"already-ready callers queue without early success");
    if(close){Check(shutdown.wait_for(5s)==std::future_status::ready&&shutdown.get(),"shutdown drains active write and all waiting callers");
        Check(!adapter.TryConfirmReceiverOwnedIfPending(mids[0],2)->Succeeded(),"new caller after close fails");
    }else{Check(successes==17&&s.Stats().confirmation_commits==2&&s.CountPending(2).count==0,
        "ready-only coordinator reduces17 independent calls to2 synced logical commits");}
}
void ReadRace(const std::filesystem::path& root){
    LocalPrivateStore s(Options(root,"read-race"));tinyimx::tests::ReadyBatchNoopRepository groups;
    LocalMessageRepositoryAdapter adapter(&s,&groups,true);std::vector<std::uint64_t> ids;
    for(int i=0;i<128;++i)ids.push_back(New(s,i));
    std::atomic<bool> done{false};std::atomic<int> bad{0};
    std::thread reader([&]{while(!done.load()){if(!s.MarkDialogRead(2,1).Succeeded())++bad;std::this_thread::yield();}});
    std::vector<std::thread> writers;
    for(int lane=0;lane<8;++lane)writers.emplace_back([&,lane]{for(std::size_t i=lane;i<ids.size();i+=8){
        if(!adapter.TryConfirmReceiverOwnedIfPending(ids[i],2)->Succeeded())++bad;
        if(!adapter.TryConfirmReceiverOwnedIfPending(ids[i],2)->Succeeded())++bad;
        if(adapter.TryConfirmReceiverOwnedIfPending(ids[i],3)->status!=MessageApplicationStatus::kPermissionDenied)++bad;
    }});
    for(auto& writer:writers)writer.join();done=true;reader.join();
    Check(s.MarkDialogRead(2,1).Succeeded()&&bad==0&&s.CountPending(2).count==0&&s.ReadUserUnreadSnapshot(2).total_unread==0,
        "concurrent read and duplicate/foreign confirmation preserve exact counters");
    const auto history=s.ListHistory(2,1,0,1000);bool monotonic=history.records.size()==128;
    for(const auto& record:history.records)monotonic&=record.delivery_state==MessageDeliveryState::kRead;
    Check(monotonic,"read state never regresses under receiver-confirmation batches");
}
void Crash(const std::filesystem::path& root,bool after,bool verify){
    const auto options=Options(root,after?"crash-after":"crash-before");
    if(!verify){LocalPrivateStore s(options,[&](auto point,auto){
        if(point==(after?LocalPrivateStore::CommitPoint::kAfterConfirmationBatchSyncBeforeReply:LocalPrivateStore::CommitPoint::kBeforeConfirmationBatchWrite)){
            std::cout<<"NATIVE_CONFIRM_REAL_CRASH_READY"<<std::endl;std::raise(SIGSTOP);}
        });auto a=New(s,0),b=New(s,1);(void)s.ConfirmReceiverRequests({{a,2},{b,2},{a,2}});throw std::runtime_error("expected controller to kill stopped child");}
    LocalPrivateStore s(options);const auto history=s.ListHistory(2,1,0,10);
    bool valid=history.Succeeded()&&history.records.size()==2;
    for(const auto& record:history.records)valid&=record.delivery_state==(after?MessageDeliveryState::kReceiverConfirmed:MessageDeliveryState::kPending);
    Check(valid&&s.CountPending(2).count==(after?0:2),"process-death recovery keeps entire confirmation batch and exact pending count");
    std::cout<<"NATIVE_CONFIRM_RECOVERY_PASS\n";
}
}
int main(int argc,char** argv){try{
    if(argc!=3)throw std::runtime_error("root and mode required");
    const auto root=std::filesystem::canonical(argv[1]);
    if(!root.filename().string().starts_with("perf854-"))throw std::runtime_error("owned fixture root required");
    const std::string mode=argv[2];
    if(mode=="contracts"){Mixed(root,true);Mixed(root,false);WriteFailure(root);InvalidNeighbor(root);Ready(root,false);Ready(root,true);ReadRace(root);
        std::cout<<"NATIVE_CONFIRM_BATCH_CONTRACTS_PASS\n";
    }else if(mode=="crash-before"||mode=="crash-after"||mode=="verify-before"||mode=="verify-after")
        Crash(root,mode.ends_with("after"),mode.starts_with("verify-"));
    else throw std::runtime_error("unknown mode");
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
