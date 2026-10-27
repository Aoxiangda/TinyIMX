#include "common/cache/RedisConnectionPool.h"
#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "services/cache/UnreadCountCache.h"
#include "services/eventing/EventCodec.h"
#include "services/projection/unread/UnreadProjector.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <unistd.h>

using namespace tinyimx::eventing;
using namespace tinyimx::projection::unread;
using namespace std::chrono_literals;
namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
ConsumedEvent Event(std::uint64_t receiver, unsigned ordinal, std::uint64_t peer=100) {
    DomainEvent e; e.schema_version=1; e.event_id="snapshot-"+std::to_string(ordinal);
    e.event_type="message.created.v1";e.aggregate_type="message";e.aggregate_id=e.event_id;
    e.producer_service="owned-snapshot-contract";e.occurred_at="2026-10-19T00:00:00.000Z";
    e.payload={{"to_user_id",receiver},{"from_user_id",peer}};
    auto encoded=EventCodec::Encode(e);Require(encoded.success,"envelope encode failed");
    ConsumedEvent c;c.topic="owned-snapshot-contract";c.tag=e.event_type;c.keys={e.event_id};
    c.body=encoded.encoded;c.broker_message_id=e.event_id;return c;
}
class Reader final: public UnreadProjectionSource {
public:
    int mode=0;std::atomic<unsigned> single_calls{0},batch_calls{0},user_calls{0};
    bool block=false,entered=false,release=false;std::mutex mutex;std::condition_variable cv;
    UnreadProjectionReadResult<DialogUnreadSnapshot> LoadDialogSnapshot(std::uint64_t u,std::uint64_t p)override {
        ++single_calls;
        if(u==20)return {false,{},"injected SQL failure"};
        if(u==11)return {true,{u+1,p,3,5},"wrong identity"};
        if(u==12)return {true,{u,p,6,5},"invalid counts"};
        return {true,{u,p,p==101?2:3,5},""};
    }
    std::vector<UnreadProjectionReadResult<DialogUnreadSnapshot>> LoadDialogSnapshots(
        const std::vector<std::pair<std::uint64_t,std::uint64_t>>& dialogs)override {
        ++batch_calls;
        if(block){std::unique_lock<std::mutex> lock(mutex);entered=true;cv.notify_all();
            Require(cv.wait_for(lock,5s,[&]{return release;}),"batch read release timed out");}
        if(mode==1)return {};
        if(mode==2)throw std::runtime_error("injected batch read exception");
        return UnreadProjectionSource::LoadDialogSnapshots(dialogs);
    }
    UnreadProjectionReadResult<UserUnreadSnapshot> LoadUserSnapshot(std::uint64_t u)override {
        ++user_calls;return {true,{u,{{100,7}},7},""};
    }
};
class Consumer final: public EventConsumer {
public:
    std::vector<ConsumedEvent> input;std::vector<std::string> acked;
    std::mutex mutex;std::condition_variable cv;bool delivered=false,ack_entered=false,hold_ack=false,release=false;
    bool fail_one_ack=false;
    EventReceiveResult Receive(std::size_t,int)override {
        std::unique_lock<std::mutex> lock(mutex);
        if(delivered){cv.wait_for(lock,1ms);return {true,{},""};}
        delivered=true;return {true,input,""};
    }
    EventAckResult Ack(const ConsumedEvent& e)override {
        std::lock_guard<std::mutex> lock(mutex);acked.push_back(e.broker_message_id);cv.notify_all();return {true,""};
    }
    std::vector<EventAckResult> AckBatch(const std::vector<ConsumedEvent>& events)override {
        std::unique_lock<std::mutex> lock(mutex);
        for(const auto& e:events)acked.push_back(e.broker_message_id);
        ack_entered=true;cv.notify_all();
        if(hold_ack)Require(cv.wait_for(lock,5s,[&]{return release;}),"transport ACK release timed out");
        std::vector<EventAckResult> r(events.size(),{true,""});
        if(fail_one_ack&&!r.empty())r.back()={false,"injected per-item transport failure"};
        return r;
    }
};
UnreadProjectorOptions Options() {
    UnreadProjectorOptions o;o.mode=UnreadProjectorMode::kWriter;o.topic="owned-snapshot-contract";
    o.batch_snapshot_reads=true;o.parallel_batch_ack=true;return o;
}
void AwaitReceived(UnreadProjector& p,std::uint64_t count) {
    const auto end=std::chrono::steady_clock::now()+5s;
    while(p.Stats().received_total<count&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(1ms);
    Require(p.Stats().received_total==count,"batch outcome processing timed out");
}
void Classified(tinyimx::RedisConnectionPool& pool,const std::string& prefix) {
    Reader reader;Consumer c;c.hold_ack=true;c.fail_one_ack=true;
    auto poison=Event(30,3);poison.body="invalid";
    c.input={Event(10,1),Event(20,2),poison,Event(11,4),Event(1034,5),Event(10,6),Event(10,7,101),Event(12,8)};
    tinyimx::UnreadCountCache cache(&pool,prefix+"classified:");
    UnreadProjector p(&c,&reader,&cache,Options());Require(p.Start(),"classified start failed");
    bool ack_started;
    {std::unique_lock<std::mutex> lock(c.mutex);ack_started=c.cv.wait_for(lock,5s,[&]{return c.ack_entered;});}
    auto stop=std::async(std::launch::async,[&]{p.Shutdown();});
    const bool held=stop.wait_for(30ms)==std::future_status::timeout;
    {std::lock_guard<std::mutex> lock(c.mutex);c.release=true;c.cv.notify_all();}stop.get();
    Require(ack_started&&held,"shutdown must join accepted transport ACKs");
    Require(c.acked==std::vector<std::string>{"snapshot-1","snapshot-5","snapshot-6","snapshot-7"},"invalid/failed event ACKed or valid event omitted");
    const auto s=p.Stats();
    Require(s.received_total==8&&s.reconciled_total==4&&s.retryable_failure_total==3&&s.poison_failure_total==1&&s.ack_failure_total==1,"classified batch stats differ");
    Require(s.snapshot_batch_calls==1&&s.snapshot_batch_dialogs==7&&reader.batch_calls==1,"batch cardinality differs");
    Require(cache.GetPrivateUnread(10,100).count==3&&cache.GetPrivateUnread(10,101).count==2&&cache.GetTotalUnread(10).count==5&&cache.GetTotalUnread(1034).count==5,"real Redis projection differs");
    for(auto u:{11u,12u,20u,30u})Require(cache.GetTotalUnread(u).count==0,"rejected snapshot mutated cache");
    std::cout<<"[PASS] real cache before ACK, poison/identity/count/SQL failures excluded, duplicate dialogs, same receiver peers, stripe collision, individual ACK failure and shutdown drain\n";
}
void FailureCases(tinyimx::RedisConnectionPool& pool,const std::string& prefix) {
    for(int mode:{1,2,3}) {
        Reader reader;reader.mode=mode;Consumer c;auto poison=Event(30,3);poison.body="invalid";
        c.input={Event(10,1),Event(1034,2),poison};
        tinyimx::RedisConnectionPool unavailable;
        tinyimx::UnreadCountCache cache(mode==3?&unavailable:&pool,prefix+"failure"+std::to_string(mode)+":");
        UnreadProjector p(&c,&reader,&cache,Options());Require(p.Start(),"failure test start failed");
        AwaitReceived(p,3);p.Shutdown();const auto s=p.Stats();
        Require(c.acked.empty()&&s.retryable_failure_total==2&&s.poison_failure_total==1&&s.reconciled_total==0,"batch dimension/exception/cache failure incorrectly ACKed");
    }
    std::cout<<"[PASS] invalid batch result size, reader exception and unavailable cache leave all affected events unacked; poison classification retained\n";
}
void ReceiverFence(tinyimx::RedisConnectionPool& pool,const std::string& prefix) {
    Reader reader;reader.block=true;Consumer c;c.input={Event(10,1),Event(1034,2)};
    tinyimx::UnreadCountCache cache(&pool,prefix+"fence:");
    UnreadProjector p(&c,&reader,&cache,Options());Require(p.Start(),"fence start failed");
    bool entered;
    {std::unique_lock<std::mutex> lock(reader.mutex);entered=reader.cv.wait_for(lock,5s,[&]{return reader.entered;});}
    std::promise<void> rebuild_started;auto ready=rebuild_started.get_future();
    auto rebuild=std::async(std::launch::async,[&]{rebuild_started.set_value();return p.RebuildUser(10);});ready.wait();
    bool fenced=rebuild.wait_for(30ms)==std::future_status::timeout&&reader.user_calls==0;
    auto stop=std::async(std::launch::async,[&]{p.Shutdown();});
    bool held=stop.wait_for(30ms)==std::future_status::timeout;
    {std::lock_guard<std::mutex> lock(reader.mutex);reader.release=true;reader.cv.notify_all();}
    stop.get();bool rebuilt=rebuild.get();
    Require(entered&&fenced&&held&&rebuilt,"read/apply receiver fence or accepted-batch shutdown contract failed");
    Require(c.acked.size()==2&&cache.GetPrivateUnread(10,100).count==7&&cache.GetTotalUnread(10).count==7,"rebuild did not execute after batch cache apply");
    Require(p.Stats().reconciled_total==2,"accepted batch was cut short at shutdown");
    std::cout<<"[PASS] sorted unique receiver locks include SQL+cache, collide safely, serialize RebuildUser and finish accepted batch during shutdown\n";
}
void Defaults(tinyimx::RedisConnectionPool& pool,const std::string& prefix) {
    Reader reader;Consumer c;c.input={Event(10,1)};tinyimx::UnreadCountCache cache(&pool,prefix+"default:");
    auto o=Options();o.batch_snapshot_reads=false;o.parallel_batch_ack=false;
    Require(!UnreadProjectorOptions{}.batch_snapshot_reads,"batching must default off");
    UnreadProjector p(&c,&reader,&cache,o);Require(p.Start(),"default start failed");AwaitReceived(p,1);p.Shutdown();
    Require(reader.single_calls==1&&reader.batch_calls==0&&c.acked.size()==1,"default per-event path changed");
    auto invalid=Options();invalid.mode=UnreadProjectorMode::kShadow;
    UnreadProjector rejected(&c,&reader,&cache,invalid);Require(!rejected.Start(),"shadow batching accepted");
    std::cout<<"[PASS] default per-event sequential ACK retained; shadow-mode batching rejected\n";
}
}
int main(int argc,char**argv) {
    if(argc!=3||std::string(argv[2])!="--owned-r58-snapshot-contract")return 2;
    tinyimx::Config config;
    if(!config.LoadFromFile(argv[1])||config.Redis().host!="codex-perf041-redis-r58"||config.Redis().db!=0)return 2;
    if(!tinyimx::Logger::Instance().Init(config.Logger()))return 2;
    tinyimx::RedisConnectionPool pool;if(!pool.Initialize(config.Redis()))return 2;
    // A stuck lifecycle test must terminate as a failure rather than hang CI.
    alarm(40);
    try {
        auto prefix="owned:snapshot-r58:"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+":";
        Classified(pool,prefix);FailureCases(pool,prefix);ReceiverFence(pool,prefix);Defaults(pool,prefix);
        alarm(0);pool.Shutdown();tinyimx::Logger::Instance().Shutdown();return 0;
    }catch(const std::exception& e){std::cerr<<"[FAIL] "<<e.what()<<'\n';pool.Shutdown();tinyimx::Logger::Instance().Shutdown();return 1;}
}
