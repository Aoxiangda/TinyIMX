#include "services/projection/unread/UnreadProjector.h"
#include "services/cache/UnreadCountCache.h"
#include "common/cache/RedisConnectionPool.h"
#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "services/eventing/EventCodec.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
using namespace tinyimx;
using namespace tinyimx::projection::unread;
using namespace tinyimx::eventing;
using namespace std::chrono_literals;

ConsumedEvent Event(std::uint64_t receiver, std::uint64_t peer, unsigned id) {
    DomainEvent e; e.event_id="message.created.v1:"+std::to_string(id);
    e.event_type="message.created.v1";e.aggregate_type="private_message";
    e.aggregate_id=std::to_string(id);e.producer_service="owned-test";
    e.occurred_at="2026-10-17T00:00:00.000Z";
    e.payload={{"message_id",id},{"from_user_id",peer},{"to_user_id",receiver}};
    auto encoded=EventCodec::Encode(e);if(!encoded.success)throw std::runtime_error(encoded.message);
    ConsumedEvent out;out.topic="tinyimx-message-events";out.tag=e.event_type;
    out.keys={e.event_id};out.body=encoded.encoded;out.broker_message_id=std::to_string(id);return out;
}
template<class F> bool Wait(F predicate, std::chrono::milliseconds budget=2500ms) {
    const auto end=std::chrono::steady_clock::now()+budget;
    while(!predicate() && std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(2ms);
    return predicate();
}
struct Source final : UnreadProjectionSource {
    std::function<void(std::uint64_t,bool)> hook;
    std::atomic<bool> fail{false}, throws{false};
    std::atomic<int> calls{0};
    UnreadProjectionReadResult<DialogUnreadSnapshot> LoadDialogSnapshot(std::uint64_t u,std::uint64_t p) override {
        ++calls;if(hook)hook(u,false);
        if(throws)throw std::runtime_error("owned dependency exception");
        if(fail)return {false,{},"owned dependency unavailable"};
        return {true,{u,p,7,14},""};
    }
    UnreadProjectionReadResult<UserUnreadSnapshot> LoadUserSnapshot(std::uint64_t u) override {
        if(hook)hook(u,true);return {true,{u,{{u+1,7},{u+2,7}},14},""};
    }
};
struct Consumer final : EventConsumer {
    std::mutex mutex;std::deque<ConsumedEvent> queue;
    std::atomic<unsigned> acked{0},bad_ack{0},held{0},max_held{0};
    std::atomic<bool> ack_fail{false},receive_fail{false};
    UnreadCountCache* cache=nullptr;
    void Add(ConsumedEvent e){std::lock_guard lock(mutex);queue.push_back(std::move(e));}
    EventReceiveResult Receive(std::size_t limit,int) override {
        EventReceiveResult r;if(receive_fail){r.message="owned receive failure";return r;}
        r.success=true;{
            std::lock_guard lock(mutex);
            while(!queue.empty()&&r.events.size()<limit){r.events.push_back(std::move(queue.front()));queue.pop_front();}
        }
        if(r.events.empty())std::this_thread::sleep_for(2ms);
        auto n=held.fetch_add(r.events.size())+r.events.size();auto m=max_held.load();
        while(n>m&&!max_held.compare_exchange_weak(m,n)){}return r;
    }
    EventAckResult Ack(const ConsumedEvent& e) override {
        if(cache){auto d=EventCodec::Decode(e.body);auto u=d.event.payload.at("to_user_id").get<std::uint64_t>();
            if(cache->GetTotalUnread(u).count!=14)++bad_ack;}
        ++acked;--held;return {!ack_fail,ack_fail?"owned ack failure":""};
    }
};
int main(int argc,char**argv){
    if(argc!=3||std::string(argv[2])!="--owned-isolated-redis")return 2;
    Config config;if(!config.LoadFromFile(argv[1])||config.Redis().host!="127.0.0.1"||config.Redis().port!=16395||config.Redis().db!=0)return 2;
    if(!Logger::Instance().Init(config.Logger()))return 2;
    RedisConnectionPool pool;if(!pool.Initialize(config.Redis()))return 2;
    const std::string prefix="codex:perf030:"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+":";
    UnreadCountCache cache(&pool,prefix);unsigned passed=0,failed=0;
    auto check=[&](bool ok,const char*name){std::cout<<(ok?"[PASS] ":"[FAIL] ")<<name<<'\n';ok?++passed:++failed;};
    auto options=[](unsigned workers=4){UnreadProjectorOptions o;o.mode=UnreadProjectorMode::kWriter;o.batch_size=1;o.worker_threads=workers;o.receive_error_backoff_ms=10;return o;};
    {
        Source s;Consumer c;for(auto w:{0u,17u}){auto o=options(w);UnreadProjector p(&c,&s,&cache,o);check(!p.Start(),"invalid worker count rejected");}
        auto o=options(16);o.batch_size=256;UnreadProjector p(&c,&s,&cache,o);check(!p.Start(),"held batch bound rejected");
    }
    {
        Source s;Consumer c;c.cache=&cache;std::atomic<int> active{0},peak{0};std::atomic<bool> release{false};
        s.hook=[&](auto,bool){auto n=++active;auto p=peak.load();while(n>p&&!peak.compare_exchange_weak(p,n)){}Wait([&]{return release.load();});--active;};
        c.Add(Event(1000,1001,1));c.Add(Event(2000,2001,2));UnreadProjector p(&c,&s,&cache,options());
        bool started=p.Start();bool concurrent=Wait([&]{return active==2;});release=true;
        check(started&&concurrent,"different receivers process concurrently");
        check(Wait([&]{return c.acked==2;})&&c.bad_ack==0,"ACK follows successful cache snapshot");p.Shutdown();
        check(c.max_held<=4,"held events bounded by workers times batch");
    }
    {
        Source s;Consumer c;c.cache=&cache;std::atomic<int> active{0},peak{0},rebuild_entered{0};std::atomic<bool> release{false};
        s.hook=[&](auto,bool rebuild){auto n=++active;auto p=peak.load();while(n>p&&!peak.compare_exchange_weak(p,n)){}if(rebuild)++rebuild_entered;Wait([&]{return release.load();});--active;};
        c.Add(Event(3000,3001,3));c.Add(Event(3000,3002,4));UnreadProjector p(&c,&s,&cache,options());if(!p.Start())return 2;
        bool first=Wait([&]{return active==1;});bool rebuilt=false;std::thread rebuild([&]{rebuilt=p.RebuildUser(3000);});
        std::this_thread::sleep_for(50ms);check(first&&peak==1&&rebuild_entered==0,"same receiver across peers and rebuild share guard");
        release=true;rebuild.join();check(Wait([&]{return c.acked==2;})&&rebuilt&&peak==1&&c.bad_ack==0,"serialized snapshots keep total and both dialogs coherent");p.Shutdown();
        check(cache.GetPrivateUnread(3000,3001).count==7&&cache.GetPrivateUnread(3000,3002).count==7&&cache.GetTotalUnread(3000).count==14,"rebuild and concurrent events retain exact projection");
    }
    {
        Source s;Consumer c;s.fail=true;c.Add(Event(4000,4001,5));UnreadProjector p(&c,&s,&cache,options());if(!p.Start())return 2;
        check(Wait([&]{return p.Stats().retryable_failure_total==1;})&&c.acked==0,"dependency failure stays unacked");p.Shutdown();
        s.fail=false;c.Add(Event(4000,4001,5));check(p.Start()&&Wait([&]{return c.acked==1;}),"redelivery succeeds after dependency recovery and restart");p.Shutdown();
    }
    {
        Source s;Consumer c;s.throws=true;c.Add(Event(5000,5001,6));UnreadProjector p(&c,&s,&cache,options());if(!p.Start())return 2;
        check(Wait([&]{return p.Stats().retryable_failure_total==1;})&&c.acked==0,"worker exception becomes retryable without ACK");p.Shutdown();
    }
    {
        Source s;Consumer c;auto e=Event(6000,6001,7);e.body="{broken";c.Add(e);UnreadProjector p(&c,&s,&cache,options());if(!p.Start())return 2;
        check(Wait([&]{return p.Stats().poison_failure_total==1;})&&s.calls==0&&c.acked==0,"poison event stays unacked without durable read");p.Shutdown();
    }
    {
        Source s;Consumer c;RedisConnectionPool closed_pool;UnreadCountCache unavailable(&closed_pool,prefix);
        c.Add(Event(7000,7001,8));UnreadProjector p(&c,&s,&unavailable,options());if(!p.Start())return 2;
        check(Wait([&]{return p.Stats().retryable_failure_total==1;})&&c.acked==0,"unavailable Redis pool stays unacked");p.Shutdown();
    }
    {
        Source s;Consumer c;c.ack_fail=true;c.Add(Event(8000,8001,9));UnreadProjector p(&c,&s,&cache,options());if(!p.Start())return 2;
        check(Wait([&]{return p.Stats().ack_failure_total==1;})&&cache.GetTotalUnread(8000).count==14,"ACK failure preserves successful durable snapshot");p.Shutdown();
    }
    {
        Source s;Consumer c;std::atomic<bool> release{false},entered{false},stopped{false};
        s.hook=[&](auto,bool){entered=true;Wait([&]{return release.load();});};
        for(unsigned i=0;i<8;++i)c.Add(Event(9000+i*2,9001+i*2,10+i));
        auto o=options(1);o.batch_size=8;UnreadProjector p(&c,&s,&cache,o);if(!p.Start())return 2;Wait([&]{return entered.load();});
        std::thread shutdown([&]{p.Shutdown();stopped=true;});std::this_thread::sleep_for(30ms);
        check(!stopped&&c.acked==0,"shutdown waits for active durable processing");release=true;shutdown.join();
        check(stopped&&c.acked==1&&p.Stats().received_total==1,"shutdown ACKs finished event and leaves rest unacked for redelivery");
    }
    {
        Source s;Consumer c;c.receive_fail=true;UnreadProjector p(&c,&s,&cache,options());if(!p.Start())return 2;
        check(Wait([&]{return p.Stats().receive_failure_total>=1;})&&c.acked==0,"receive failures counted and never acknowledged");p.Shutdown();
    }
    pool.Shutdown();std::cout<<"RESULT "<<passed<<" PASS "<<failed<<" FAIL\n";return failed?1:0;
}
