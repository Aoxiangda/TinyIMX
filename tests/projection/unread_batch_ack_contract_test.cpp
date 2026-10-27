#include "common/cache/RedisConnectionPool.h"
#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "services/cache/UnreadCountCache.h"
#include "services/eventing/EventCodec.h"
#include "services/projection/unread/UnreadProjector.h"

#include <chrono>
#include <condition_variable>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>

using namespace tinyimx::eventing;
using namespace tinyimx::projection::unread;
using namespace std::chrono_literals;
namespace {
void Require(bool value,const char* text) {if(!value)throw std::runtime_error(text);}
ConsumedEvent Event(std::uint64_t receiver) {
    DomainEvent event; event.schema_version=1; event.event_id="ack-contract-"+std::to_string(receiver);
    event.event_type="message.created.v1"; event.aggregate_type="message";
    event.aggregate_id=event.event_id; event.producer_service="owned-ack-contract";
    event.occurred_at="2026-10-19T00:00:00.000Z";
    event.payload={{"to_user_id",receiver},{"from_user_id",100}};
    auto encoded=EventCodec::Encode(event); Require(encoded.success,"test envelope invalid");
    ConsumedEvent consumed; consumed.topic="owned-ack-contract";consumed.tag=event.event_type;
    consumed.broker_message_id=event.event_id; consumed.keys={event.event_id};consumed.body=encoded.encoded;
    return consumed;
}
class Reader final: public UnreadProjectionSource {
public:
    UnreadProjectionReadResult<DialogUnreadSnapshot> LoadDialogSnapshot(std::uint64_t u,std::uint64_t p)override {
        if(u==20)return {false,{},"injected durable read failure"};
        return {true,{u,p,3,5},""};
    }
    UnreadProjectionReadResult<UserUnreadSnapshot> LoadUserSnapshot(std::uint64_t)override{return {};}
};
class Consumer final: public EventConsumer {
public:
    std::mutex mutex;std::condition_variable cv;bool delivered=false,started=false,release=false;
    std::vector<std::string> acked;
    EventReceiveResult Receive(std::size_t,int)override {
        std::lock_guard<std::mutex> lock(mutex);
        if(delivered)return {true,{},""};
        delivered=true;auto poison=Event(30);poison.body="invalid";
        return {true,{Event(10),Event(20),poison,Event(11)},""};
    }
    EventAckResult Ack(const ConsumedEvent&)override {throw std::runtime_error("sequential ACK used in opt-in mode");}
    std::vector<EventAckResult> AckBatch(const std::vector<ConsumedEvent>& events)override {
        std::unique_lock<std::mutex> lock(mutex);
        for(const auto& event:events)acked.push_back(event.broker_message_id);
        started=true;cv.notify_all();cv.wait(lock,[&]{return release;});
        return {{true,""},{false,"injected individual transport failure"}};
    }
};
class Fallback final: public EventConsumer {
public:
    EventReceiveResult Receive(std::size_t,int)override{return {};}
    EventAckResult Ack(const ConsumedEvent& event)override {
        if(event.broker_message_id=="throw")throw std::runtime_error("one failed slot");
        return {event.broker_message_id=="ok", ""};
    }
};
}
int main(int argc,char**argv) {
    if(argc!=3 || std::string(argv[2])!="--owned-r27-redis")return 2;
    tinyimx::Config config;
    if(!config.LoadFromFile(argv[1]) || config.Redis().host!="codex-perf041-redis-r27" ||config.Redis().db!=0)return 2;
    if(!tinyimx::Logger::Instance().Init(config.Logger()))return 2;
    tinyimx::RedisConnectionPool pool;
    if(!pool.Initialize(config.Redis()))return 2;
    try {
        Fallback fallback;std::vector<ConsumedEvent> input(3);
        input[0].broker_message_id="ok";input[1].broker_message_id="throw";input[2].broker_message_id="fail";
        auto original=fallback.AckBatch(input);
        Require(original.size()==3 && original[0].success && !original[1].success && !original[2].success,
                "sequential fallback lost individual outcomes");
        Reader reader;Consumer consumer;
        const auto nonce=std::chrono::steady_clock::now().time_since_epoch().count();
        tinyimx::UnreadCountCache cache(&pool,"owned:ack-contract:"+std::to_string(nonce)+":");
        UnreadProjectorOptions options;Require(!options.parallel_batch_ack,"default must remain off");
        options.parallel_batch_ack=true;options.mode=UnreadProjectorMode::kWriter;options.topic="owned-ack-contract";
        UnreadProjector projector(&consumer,&reader,&cache,options);
        Require(projector.Start(),"projector failed to start");
        bool started;
        {std::unique_lock<std::mutex> lock(consumer.mutex);started=consumer.cv.wait_for(lock,5s,[&]{return consumer.started;});}
        auto shutdown=std::async(std::launch::async,[&]{projector.Shutdown();});
        bool held=shutdown.wait_for(30ms)==std::future_status::timeout;
        {std::lock_guard<std::mutex> lock(consumer.mutex);consumer.release=true;consumer.cv.notify_all();}
        shutdown.get();
        Require(started && held,"shutdown returned before pending batch completed");
        Require(consumer.acked==std::vector<std::string>{"ack-contract-10","ack-contract-11"},
                "poison/dependency-failed event was ACKed or successful event lost");
        const auto stats=projector.Stats();
        Require(stats.received_total==4 && stats.reconciled_total==2 && stats.retryable_failure_total==1 &&
                stats.poison_failure_total==1 && stats.ack_failure_total==1,"classified stats differ");
        for(auto u:{10u,11u})Require(cache.GetPrivateUnread(u,100).count==3 && cache.GetTotalUnread(u).count==5,
                                   "ACK occurred before successful real Redis projection");
        Require(cache.GetTotalUnread(20).count==0 && cache.GetTotalUnread(30).count==0,
                "failed event mutated cache");
        pool.Shutdown();tinyimx::Logger::Instance().Shutdown();
        std::cout<<"[PASS] real Redis projection before ACK; poison/read failure not ACKed; per-item failure counted; shutdown joins pending ACK; default sequential fallback preserved\n";
        return 0;
    }catch(const std::exception&e) {
        std::cerr<<"[FAIL] "<<e.what()<<'\n';pool.Shutdown();tinyimx::Logger::Instance().Shutdown();return 1;
    }
}
