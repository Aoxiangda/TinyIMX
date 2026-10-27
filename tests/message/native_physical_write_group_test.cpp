#include "services/message/local/LocalPrivateStore.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

using namespace tinyimx::message;
using namespace tinyimx::message::local;
namespace {
using Json=nlohmann::json;
void Check(bool good,const char* text){
    if(!good)throw std::runtime_error(text);
    std::cout<<"PASS "<<text<<std::endl;
}
template<class Predicate>void Wait(Predicate predicate,const char* text){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!predicate()){
        if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error(text);
        std::this_thread::yield();
    }
}
unsigned Kind(unsigned i){return(i==2||i==5)?2:(i%3==1?1:0);}
std::string NewCID(unsigned i){return "ready-new-"+std::to_string(i);}
std::string NewBody(unsigned i){return "ready-body-"+std::to_string(i);}
struct Plan {
    std::vector<std::uint64_t> mids;
    std::vector<std::string> events;
    std::vector<std::string> published;
};
Plan Warm(LocalPrivateStore& store){
    Plan plan;std::set<std::string> known;
    for(unsigned i=0;i<16;++i){
        const auto result=store.PersistPrivateMessage(2*i+1,2*i+2,"warm-"+std::to_string(i),1,"warm-body");
        Check(result.Accepted(),"warm message durable before physical gate is armed");
        plan.mids.push_back(result.message_id);
        const auto pending=store.ListPendingEvents(256);Check(pending.Succeeded(),"read exact owned pending events");
        std::string added;
        for(const auto& event:pending.events)if(!known.contains(event.event_id)){
            Check(added.empty(),"exactly one event added by each warm message");added=event.event_id;
        }
        Check(!added.empty(),"warm message has its durable event");known.insert(added);plan.events.push_back(added);
    }
    plan.published.push_back(plan.events[2]);
    const auto first=std::hash<std::string_view>{}(plan.events[2])%64;
    for(const auto& event:plan.events)if(std::hash<std::string_view>{}(event)%64!=first){
        plan.published.push_back(event);break;
    }
    Check(plan.published.size()==2,"two publication locks use distinct actual event stripes");
    return plan;
}
void Verify(LocalPrivateStore& store,const Plan& plan,bool after){
    unsigned creates=0;
    for(unsigned i=0;i<16;++i){
        const auto warm=store.GetPrivateMessage(plan.mids[i]);
        Check(warm.Found()&&warm.record.content=="warm-body","warm record and body survive physical group");
        Check((warm.record.delivery_state==MessageDeliveryState::kReceiverConfirmed)==(after&&Kind(i)==1),
            "receiver-confirm state matches the actual physical boundary");
        const bool expected=Kind(i)==0&&(after||i==0);
        const auto found=store.FindPrivateMessageByClientMessageId(2*i+1,NewCID(i));
        Check(found.Succeeded()&&found.Found()==expected,"each create CID has exactly the expected durability");
        if(expected){
            ++creates;Check(found.record.content==NewBody(i),"created body is byte exact");
            const auto retry=store.PersistPrivateMessage(2*i+1,2*i+2,NewCID(i),1,NewBody(i));
            Check(retry.outcome==PersistPrivateMessageOutcome::kReused&&retry.message_id==found.record.message_id,
                "durable CID resolves to its original MID without another create");
        }
        const auto count=store.CountPending(2*i+2);
        const auto wanted=1u+(expected?1u:0u)-(after&&Kind(i)==1?1u:0u);
        Check(count.Succeeded()&&count.count==wanted,"receiver pending counter agrees with durable records");
    }
    Check(creates==(after?9u:1u),"exact number of new records across the physical write boundary");
    const auto events=store.ListPendingEvents(256);Check(events.Succeeded(),"read final event index");
    std::set<std::string> ids;for(const auto& event:events.events)ids.insert(event.event_id);
    for(const auto& id:plan.published)
        Check(ids.contains(id)==!after,"publication marker is recovered with the same mixed write");
    Check(ids.size()==(after?23u:17u),"exact create and publication event indexes after recovery");
}
LocalPrivateStoreOptions Options(const std::filesystem::path& root){
    LocalPrivateStoreOptions options;
    options.directory=(root/"owner").string();options.storage_identity="physical-ready-"+root.filename().string();
    options.message_events_topic="tinyimx-message-events";options.first_message_id=7813700000000000ULL;
    for(std::uint64_t id=1;id<=32;++id)options.initial_user_ids.push_back(id);
    options.exact_user_locking=true;options.persist_batch_enable=true;
    options.block_cache_bytes=8*1024*1024;options.max_pending_events=512;
    return options;
}
Plan Load(const std::filesystem::path& root){
    Json json;std::ifstream input(root/"plan.json");input>>json;
    return {json.at("mids").get<std::vector<std::uint64_t>>(),
        json.at("events").get<std::vector<std::string>>(),json.at("published").get<std::vector<std::string>>()};
}
}
int main(int argc,char** argv){
    try{
        if(argc!=4)return 2;
        const std::filesystem::path root=argv[1];const std::string flag=argv[2],mode=argv[3];
        Check(flag=="0"||flag=="1","exact physical factor");
        const char* actual=std::getenv("TINYIMX_NATIVE_READY_PHYSICAL_WRITE_GROUP_ENABLE");
        const char* shards=std::getenv("TINYIMX_NATIVE_EVENT_PROGRESS_SHARDS_ENABLE");
        Check(actual&&flag==actual&&shards&&std::string(shards)=="1","actual independent physical and event-lock flags");
        const std::string category=mode=="contracts"?"contracts":mode=="uncertain"?"uncertain":
            mode.ends_with("-before")?"before":mode.ends_with("-after")?"after":"invalid";
        const auto parent=root.parent_path().string();
        Check((parent=="/mnt/tinyimx-benchmark/committed-unread-perf1598-r1"||parent=="/mnt/tinyimx-benchmark/committed-unread-perf1598-r2"||parent=="/mnt/tinyimx-benchmark/committed-unread-perf1598-r3"||parent=="/mnt/tinyimx-benchmark/sst-writeback-perf1550-r1"||parent=="/mnt/tinyimx-benchmark/sst-writeback-perf1550-r2"||parent=="/mnt/tinyimx-benchmark/engine-wait-trace-perf1520-r1"||parent=="/mnt/tinyimx-benchmark/engine-wait-trace-perf1520-r2"||parent=="/mnt/tinyimx-benchmark/keyed-ready-scheduler-perf1490-r1"||parent=="/mnt/tinyimx-benchmark/keyed-ready-scheduler-perf1490-r2"||parent=="/mnt/tinyimx-benchmark/keyed-ready-scheduler-perf1490-r3"||parent=="/mnt/tinyimx-benchmark/front-preparation-limit-perf1450-r1"||parent=="/mnt/tinyimx-benchmark/front-preparation-limit-perf1450-r2"||parent=="/mnt/tinyimx-benchmark/front-preparation-limit-perf1450-r3"||parent=="/mnt/tinyimx-benchmark/relay-capability-take-perf1405-r1"||parent=="/mnt/tinyimx-benchmark/relay-capability-take-perf1405-r2"||parent=="/mnt/tinyimx-benchmark/relay-capability-take-perf1405-r3"||parent=="/mnt/tinyimx-benchmark/physical-write-group-perf1370-r1"||
            parent=="/mnt/tinyimx-benchmark/physical-write-group-perf1370-r2"||
            parent=="/mnt/tinyimx-benchmark/physical-write-group-perf1370-r3")&&
            root.filename()=="c"+flag+"-"+category&&std::filesystem::canonical(root)==root,
            "exact new owned physical fixture");
        Check(mode=="contracts"||flag=="1","fault boundary tests require enabled physical coordinator");
        const auto options=Options(root);
        if(mode.starts_with("verify-")){
            const auto plan=Load(root);LocalPrivateStore store(options);
            Verify(store,plan,mode=="verify-after");Check(store.Shutdown(),"recovered owner closes");
            std::cout<<"NATIVE_PHYSICAL_GROUP_REAL_RECOVERY_PASS "<<mode<<std::endl;return 0;
        }
        Plan plan;
        {
            std::atomic<bool> armed{false};std::atomic<unsigned> before_calls{0};
            std::promise<void> entered,release;const auto released=release.get_future().share();
            auto observer=[&](LocalPrivateStore::CommitPoint point,std::uint64_t key_ops){
                if(!armed.load())return;
                if(point==LocalPrivateStore::CommitPoint::kBeforeEngineWrite){
                    const auto number=++before_calls;
                    if(number==1){entered.set_value();released.wait();}
                    if(number==2&&mode=="crash-before"){
                        std::cout<<"NATIVE_PHYSICAL_GROUP_REAL_CRASH_READY before key_ops="<<key_ops<<std::endl;std::raise(SIGSTOP);
                    }
                }
                if(point==LocalPrivateStore::CommitPoint::kAfterEngineWriteBeforeWake&&before_calls.load()==2){
                    if(mode=="crash-after"){
                        std::cout<<"NATIVE_PHYSICAL_GROUP_REAL_CRASH_READY after key_ops="<<key_ops<<std::endl;std::raise(SIGSTOP);
                    }
                    if(mode=="uncertain")throw std::runtime_error("owned uncertain reply after physical commit");
                }
            };
            LocalPrivateStore store(options,observer);
            Check(store.EventProgressShardsEnabled(),"actual event stripes enabled in both arms");
            plan=Warm(store);
            {
                std::ofstream output(root/"plan.json");
                output<<Json{{"mids",plan.mids},{"events",plan.events},{"published",plan.published}}.dump();
                output.flush();Check(output.good(),"write exact warm IDs before controlled process death");
            }
            const auto start=store.PhysicalWriteGroupStats();
            const auto start_syncs=store.Stats().engine_wal_syncs;armed=true;
            const auto call=[&](unsigned i){
                if(Kind(i)==0)return store.PersistPrivateMessage(2*i+1,2*i+2,NewCID(i),1,NewBody(i)).Accepted();
                if(Kind(i)==1)return store.ConfirmReceiver(plan.mids[i],2*i+2).Succeeded();
                return store.MarkEventsPublished({plan.published[i==2?0:1]}).Succeeded();
            };
            std::vector<std::future<bool>> calls(16);
            calls[0]=std::async(std::launch::async,call,0);
            Check(entered.get_future().wait_for(std::chrono::seconds(5))==std::future_status::ready,
                "first actual engine entrance reached controlled gate");
            try{
                // The first follower is deliberately an unsynced publication
                // marker. Later synced creates/confirms must strengthen its
                // whole physical group, regardless of the first input's flag.
                calls[2]=std::async(std::launch::async,call,2);
                if(flag=="1")Wait([&]{return store.PhysicalWriteGroupStats().queued==1;},"first publication follower was not queued");
                else Check(calls[2].wait_for(std::chrono::seconds(5))==std::future_status::ready,"original unsynced progress entrance completes");
                for(unsigned i=1;i<16;++i)if(i!=2)calls[i]=std::async(std::launch::async,call,i);
                if(flag=="1"){
                    Wait([&]{return store.PhysicalWriteGroupStats().queued==15;},"mixed native writes did not reach physical ready queue");
                    for(unsigned i=1;i<16;++i)Check(calls[i].wait_for(std::chrono::milliseconds(0))==std::future_status::timeout,
                        "native caller still waits for shared physical result");
                }else{
                    for(unsigned i=1;i<16;++i)Check(calls[i].wait_for(std::chrono::seconds(5))==std::future_status::ready,
                        "original engine entrance remains independent without coordinator");
                }
            }catch(...){release.set_value();for(auto& call:calls)if(call.valid())call.wait();throw;}
            release.set_value();
            for(unsigned i=0;i<16;++i)Check(calls[i].get()==(mode!="uncertain"||i==0),
                "caller success follows its physical result without replay");
            Check(mode=="contracts"||mode=="uncertain","crash fixture must have stopped at actual physical boundary");
            const auto end=store.PhysicalWriteGroupStats();
            Check(end.engine_write_calls-start.engine_write_calls==(flag=="1"?2u:16u)&&
                end.queued==0&&end.held_input_bytes==0,"real engine calls and mixed caller ownership reconcile");
            if(flag=="1")Check(store.Stats().engine_wal_syncs-start_syncs>=2,
                "synced members strengthen first unsynced follower in actual WAL group");
            if(mode=="uncertain"){
                Check(store.Stats().poisoned&&!store.Healthy()&&!store.PersistPrivateMessage(1,2,"fenced",1,"must not commit").Accepted(),
                    "uncertain shared outcome fences subsequent new work");
                Check(!store.Shutdown(),"uncertain owner requires reopen before acceptance");
            }else{
                Verify(store,plan,true);Check(store.Shutdown(),"mixed physical owner closes");
            }
        }
        if(mode=="uncertain"){
            LocalPrivateStore restored(options);Verify(restored,plan,true);
            Check(restored.Shutdown(),"uncertain completed group recovers all exact effects");
        }
        std::cout<<"NATIVE_PHYSICAL_GROUP_MIXED_ENGINE_CONTRACTS_PASS flag="<<flag<<" mode="<<mode<<std::endl;
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
