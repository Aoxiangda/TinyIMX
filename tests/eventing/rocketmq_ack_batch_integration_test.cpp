#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "services/eventing/rocketmq/RocketMQProducer.h"
#include "services/eventing/rocketmq/RocketMQSimpleConsumer.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace tinyimx::eventing;
using namespace tinyimx::eventing::rocketmq_transport;
using namespace std::chrono_literals;
namespace {
void Require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
}

int main(int argc, char** argv) {
    if ((argc != 3 && argc != 4) || std::string(argv[2]) != "--owned-r27-ack-batch") return 2;
    const bool resume = argc == 4 && std::string(argv[3]) == "--resume-owned32";
    if (argc == 4 && !resume) return 2;
    tinyimx::Config config;
    if (!config.LoadFromFile(argv[1]) ||
        config.RocketMQ().message_topic != "codex_perf041_events_20261019_r27" ||
        config.UnreadProjection().consumer_group != "codex_perf041_consumer_20261019_r27") return 2;
    if (!tinyimx::Logger::Instance().Init(config.Logger())) return 2;
    RocketMQSimpleConsumerOptions co;
    co.endpoint=config.RocketMQ().endpoint; co.topic=config.RocketMQ().message_topic;
    co.consumer_group=config.UnreadProjection().consumer_group;
    co.request_timeout_ms=config.RocketMQ().request_timeout_ms;
    co.await_duration_ms=config.UnreadProjection().await_duration_ms;
    co.tls=config.RocketMQ().tls; co.access_key=config.RocketMQ().access_key;
    co.access_secret=config.RocketMQ().access_secret;
    RocketMQProducerOptions po;
    po.endpoint=co.endpoint; po.topic=co.topic; po.request_timeout_ms=co.request_timeout_ms;
    po.tls=co.tls; po.access_key=co.access_key; po.access_secret=co.access_secret;
    RocketMQSimpleConsumer consumer(co);
    RocketMQProducer producer(po);
    try {
        Require(consumer.Start(), "owned consumer start failed");
        Require(producer.Start(), "owned producer start failed");
        Require(consumer.AckBatch({}).empty(), "empty ACK batch failed");
        auto oversized=consumer.AckBatch(std::vector<ConsumedEvent>(257));
        Require(oversized.size()==257 && std::none_of(oversized.begin(),oversized.end(),
                [](const auto& x){return x.success;}), "oversize must reject all before submit");
        std::map<std::string,std::string> expected;
        std::map<std::string,std::string> body_keys, body_owners;
        for(int i=0;i<32;++i) {
            PublishMessage m; m.event_id="perf064-ack-"+std::to_string(i);
            m.message_key=m.event_id; m.topic=co.topic; m.tag="perf064.ack.batch.v1";
            m.body="exact-body:"+std::to_string(i);
            body_keys.emplace(m.body,m.message_key);
            if(resume) continue; // Only the original32 from the preserved failed run.
            auto sent=producer.Publish(m);
            Require(sent.Published(), "owned publish failed");
            Require(expected.emplace(sent.broker_message_id,m.body).second,"unique broker ID required");
        }
        std::map<std::string,int> completed;
        std::string intentionally_unacked;
        bool redelivered=false;
        std::size_t maximum_batch=0;
        const auto deadline=std::chrono::steady_clock::now()+100s;
        while(std::chrono::steady_clock::now()<deadline && completed.size()<32) {
            auto receive=consumer.Receive(16,10000);
            if(!receive.success)throw std::runtime_error("owned receive failed: "+receive.message);
            maximum_batch=std::max(maximum_batch,receive.events.size());
            if(receive.events.empty()) {std::this_thread::sleep_for(30ms);continue;}
            for(const auto& event:receive.events) {
                if(resume && !expected.count(event.broker_message_id)) {
                    Require(body_keys.count(event.body)==1,"foreign body in resumed owned32");
                    Require(std::find(event.keys.begin(),event.keys.end(),body_keys.at(event.body))!=event.keys.end(),
                            "resumed event key does not match exact body");
                    auto [owner,inserted]=body_owners.emplace(event.body,event.broker_message_id);
                    Require(inserted || owner->second==event.broker_message_id,"two IDs for one expected body");
                    expected.emplace(event.broker_message_id,event.body);
                }
                Require(expected.count(event.broker_message_id)==1,"foreign event in isolated group");
                Require(event.topic==co.topic && event.tag=="perf064.ack.batch.v1" &&
                    event.body==expected.at(event.broker_message_id),"transport payload changed");
                Require(!completed.count(event.broker_message_id),"already ACKed event unexpectedly redelivered");
            }
            bool inject=intentionally_unacked.empty();
            if(inject) {
                intentionally_unacked=receive.events.front().broker_message_id;
                receive.events.front().native_handle.reset();
            }
            auto ack=consumer.AckBatch(receive.events);
            Require(ack.size()==receive.events.size(),"per-event ACK result count differs");
            for(std::size_t i=0;i<ack.size();++i) {
                const auto& id=receive.events[i].broker_message_id;
                if(inject && i==0) {Require(!ack[i].success,"missing receipt falsely ACKed");continue;}
                Require(ack[i].success,"valid receipt ACK failed");
                completed[id]++;
                if(id==intentionally_unacked) redelivered=true;
            }
        }
        Require(maximum_batch>=2,"real parallel multi-event batch was not exercised");
        Require(completed.size()==32 && redelivered,"unacked event did not recover by real redelivery");
        consumer.Stop(); // Only after AckBatch has joined all callbacks.
        auto stopped=consumer.AckBatch(std::vector<ConsumedEvent>(2));
        Require(stopped.size()==2 && !stopped[0].success && !stopped[1].success,
                "stopped transport cannot report ACK success");
        producer.Stop();
        std::cout<<"[PASS] real MQ:32 exact bodies; bounded multi-event async ACK; one missing receipt remains unacked; real redelivery recovered; stopped transport fails; max_batch="<<maximum_batch<<'\n';
        tinyimx::Logger::Instance().Shutdown();
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"[FAIL] "<<e.what()<<'\n';
        producer.Stop(); consumer.Stop(); tinyimx::Logger::Instance().Shutdown(); return 1;
    }
}
