#include "services/message/local/LocalEventRelay.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
using namespace tinyimx::message;
using namespace tinyimx::message::local;
using namespace tinyimx::eventing;
using namespace std::chrono_literals;
namespace {
void Check(bool good,const char* why){
    if(!good)throw std::runtime_error(why);
    std::cout<<"PASS "<<why<<std::endl;
}
template<class F>bool Wait(F predicate){
    const auto until=std::chrono::steady_clock::now()+5s;
    while(std::chrono::steady_clock::now()<until){
        if(predicate())return true;std::this_thread::sleep_for(1ms);
    }
    return predicate();
}
LocalPrivateStoreOptions StoreOptions(const std::filesystem::path& root,const char* name){
    LocalPrivateStoreOptions o;o.directory=(root/name).string();
    o.storage_identity=root.filename().string()+":"+name;o.first_message_id=7840400000000000ULL;
    o.message_events_topic="codex_perf1559_events";o.initial_user_ids={500001,500002};
    o.exact_user_locking=true;o.block_cache_bytes=1024*1024;return o;
}
void Fill(LocalPrivateStore& store,unsigned n){
    for(unsigned i=0;i<n;++i)
        Check(store.PersistPrivateMessage(500001,500002,"take-"+std::to_string(i),1,"body-"+std::to_string(i)).Accepted(),
            "seed one exact durable event");
}
class GatePublisher final:public EventPublisher {
public:
    std::mutex mutex;std::condition_variable changed;
    unsigned entered{0};bool first_released{false},all_released{false};
    std::map<std::string,std::string> bodies;
    PublishResult Publish(const PublishMessage& m)override{
        std::unique_lock lock(mutex);const auto call=++entered;changed.notify_all();
        changed.wait(lock,[&]{return all_released||(call==1&&first_released);});
        if(!bodies.emplace(m.event_id,m.body).second)throw std::runtime_error("duplicate successful publication");
        return{PublishStatus::kPublished,m.event_id,""};
    }
    unsigned Entered(){std::lock_guard lock(mutex);return entered;}
    void ReleaseFirst(){std::lock_guard lock(mutex);first_released=true;changed.notify_all();}
    void ReleaseAll(){std::lock_guard lock(mutex);all_released=true;changed.notify_all();}
    std::size_t Count(){std::lock_guard lock(mutex);return bodies.size();}
};
class TwoPublisher final:public EventPublisher {
public:
    std::mutex mutex;unsigned batches{0},scalars{0};std::size_t largest{0};
    std::map<std::string,std::string> bodies;
    std::size_t MaxBatchMessages()const noexcept override{return 2;}
    PublishResult Accept(const PublishMessage&m){
        if(!bodies.emplace(m.event_id,m.body).second)throw std::runtime_error("duplicate two-capability publication");
        return{PublishStatus::kPublished,m.event_id,""};
    }
    PublishResult Publish(const PublishMessage&m)override{
        std::lock_guard lock(mutex);++scalars;return Accept(m);
    }
    std::vector<PublishResult> PublishBatch(const std::vector<PublishMessage>&m)override{
        std::lock_guard lock(mutex);
        if(m.empty()||m.size()>2)throw std::runtime_error("transport capability exceeded");
        ++batches;largest=std::max(largest,m.size());std::vector<PublishResult> out;
        for(const auto&x:m)out.push_back(Accept(x));return out;
    }
};
}
int main(int argc,char**argv)try{
    if(argc!=3)return 2;
    const std::filesystem::path root=argv[1];const std::string flag=argv[2];
    const char*actual=std::getenv("TINYIMX_NATIVE_RELAY_CAPABILITY_TAKE_ENABLE");
    const char*detached=std::getenv("TINYIMX_NATIVE_RELAY_DETACHED_PROGRESS_ENABLE");
    Check((flag=="0"||flag=="1")&&actual&&flag==actual&&detached&&std::string(detached)=="1",
        "actual independent take factor and detached writer");
    Check(root.parent_path()=="/mnt/tinyimx-benchmark"&&std::filesystem::canonical(root)==root&&
        (root.filename()=="perf1559-r1-d"+flag||root.filename()=="perf1559-r2-d"+flag||
         root.filename()=="perf1559-r3-d"+flag),"exact new owned capability fixture");
    {
        LocalPrivateStore store(StoreOptions(root,"scalar"));Fill(store,128);
        GatePublisher publisher;LocalEventRelayOptions o;
        o.workers=4;o.max_held=64;o.scan_page=128;o.progress_batch_size=16;
        o.nonblocking_health=true;o.poll_interval=1ms;o.sync_interval=20ms;
        LocalEventRelay relay(&store,&publisher,o);
        struct Release {GatePublisher&p;~Release(){p.ReleaseAll();}} release{publisher};
        Check(relay.Start()&&Wait([&]{return publisher.Entered()==4&&relay.Stats().held==64;}),
            "four real scalar calls blocked with exact held64 bound");
        Check(relay.Stats().queued==(flag=="1"?60u:0u)&&store.Stats().pending_events==128,
            "take1 leaves sixty ready events queued; legacy take16 reserves all64");
        publisher.ReleaseFirst();
        Check(Wait([&]{return publisher.Entered()==5;}),"first sender advances to its second real call");
        if(flag=="1")Check(Wait([&]{return relay.Stats().published==1;}),
            "first scalar success is marked before the next scalar RPC completes");
        else Check(relay.Stats().published==0,
            "legacy scalar loop withholds first success until its reserved group ends");
        auto stop=std::async(std::launch::async,[&]{return relay.Shutdown();});
        const bool stopping=Wait([&]{return relay.Stats().stopping;});
        const bool waited=stop.wait_for(10ms)==std::future_status::timeout;
        publisher.ReleaseAll();const bool closed=stop.get();
        Check(stopping&&waited&&closed,"stop fences new calls and drains the four actual in-flight calls");
        Check(publisher.Count()==5&&relay.Stats().published==5&&relay.Stats().held==0&&
            relay.Stats().held_peak==64&&store.Stats().pending_events==123,
            "five successes alone retire; every unattempted event stays durable and bounded");
        Check(store.ReadUnreadSnapshot(500002,500001).total_unread==128,
            "publication scheduling never changes unread truth");
        GatePublisher next;next.ReleaseAll();LocalEventRelay resumed(&store,&next,o);
        Check(resumed.Start()&&Wait([&]{return store.Stats().pending_events==0;})&&resumed.Shutdown(),
            "next owner publishes every remaining durable event");
        Check(next.Count()==123&&std::none_of(next.bodies.begin(),next.bodies.end(),
            [&](const auto&x){return publisher.bodies.contains(x.first);}),
            "successful IDs are not republished after clean shutdown");
        Check(store.Shutdown(),"scalar fixture closes");
    }
    {
        LocalPrivateStore store(StoreOptions(root,"cap2"));Fill(store,32);TwoPublisher publisher;
        LocalEventRelayOptions o;o.workers=2;o.max_held=16;o.scan_page=64;o.progress_batch_size=16;
        o.nonblocking_health=true;o.poll_interval=1ms;o.sync_interval=20ms;
        LocalEventRelay relay(&store,&publisher,o);
        Check(relay.Start()&&Wait([&]{return store.Stats().pending_events==0;})&&relay.Shutdown(),
            "transport capped at two drains all exact events");
        Check(publisher.bodies.size()==32&&relay.Stats().published==32&&relay.Stats().held_peak<=16,
            "small transport batches preserve exact identities and ownership bound");
        if(flag=="1")Check(publisher.batches>0&&publisher.largest==2,
            "ready take fits capability2 instead of forcing a scalar16 fallback");
        else Check(publisher.batches==0&&publisher.scalars==32,
            "legacy incompatible sixteen-item take uses scalar compatibility path");
        Check(store.Shutdown(),"capability2 fixture closes");
    }
    std::cout<<"RELAY_CAPABILITY_TAKE_REAL_NATIVE_CONTRACTS_PASS flag="<<flag<<std::endl;return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
