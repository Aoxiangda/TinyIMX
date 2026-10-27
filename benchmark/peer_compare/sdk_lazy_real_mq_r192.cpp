// Actual isolated SDK OFF/ON correctness, never a capacity result.
#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "services/eventing/rocketmq/RocketMQProducer.h"
#include "services/eventing/rocketmq/RocketMQSimpleConsumer.h"
#include <rocketmq/Logger.h>
#include <dlfcn.h>
#include <filesystem>
#include <cstdlib>
#include <unistd.h>

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
    if ((argc != 5 && argc != 6) || std::string(argv[2]) != "--owned-sdk192") return 2;
    const bool validate_only=argc==6 && std::string(argv[5])=="--validate-owned-config";
    if(argc==6&&!validate_only)return 2;
    const std::string cohort=argv[3];
    if(cohort!="off1"&&cohort!="on1"&&cohort!="on2"&&cohort!="off2")return 2;
    const bool lazy=cohort=="on1"||cohort=="on2";
    const char* actual_flag=std::getenv("TINYIMX_ROCKETMQ_LAZY_DEBUG_ENABLE");
    if(!actual_flag||std::string(actual_flag)!=(lazy?"1":"0"))return 2;
    Dl_info info{};
    void* logger_symbol=dlsym(RTLD_DEFAULT,"_ZN8rocketmq9getLoggerEv");
    if(!logger_symbol||!dladdr(logger_symbol,&info)||!info.dli_fname)return 2;
    const std::filesystem::path candidate=std::filesystem::canonical(argv[4]);
    if(!candidate.is_absolute()||!candidate.string().starts_with("/home/jackson7/projects/tinyimx-interview-20261017/perf191-sdk-lazy-build-r1/prefix/lib/")||
       std::filesystem::canonical(info.dli_fname)!=candidate)return 2;
    tinyimx::Config config;
    if(!config.LoadFromFile(argv[1])) {
        std::cerr<<"SDK192_CONFIG_REJECTED "<<config.LastError()<<std::endl;
        return 2;
    }
    if(config.RocketMQ().message_topic != "codex_perf192_events_20261021_r3_"+cohort ||
       config.UnreadProjection().consumer_group != "codex_perf192_consumer_20261021_r3_"+cohort)return 2;
    if(validate_only) {
        std::cout<<"SDK192_CONFIG_VALIDATED_NO_MQ_CLIENT_CREATED"<<std::endl;
        return 0;
    }
    // Fix the sink threshold at initialization; keep the actual message test's
    // runtime threshold INFO, then exercise real DEBUG formatting after ACKs.
    const std::string log_owner="SDK192_"+cohort+"_"+std::to_string(getpid());
    ROCKETMQ_NAMESPACE::getLogger().setLevel(ROCKETMQ_NAMESPACE::Level::Debug);
    ROCKETMQ_NAMESPACE::getLogger().setConsoleLevel(ROCKETMQ_NAMESPACE::Level::Warn);
    ROCKETMQ_NAMESPACE::getLogger().setPattern(log_owner+" %v");
    ROCKETMQ_NAMESPACE::getLogger().init();
    ROCKETMQ_NAMESPACE::getLogger().setLevel(ROCKETMQ_NAMESPACE::Level::Info);
    std::cout<<"SDK192_LOG_OWNER "<<log_owner<<std::endl;
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
        std::map<std::string,std::string> body_keys;
        for(int i=0;i<32;++i) {
            PublishMessage m; m.event_id="perf192-"+cohort+"-ack-"+std::to_string(i);
            m.message_key=m.event_id; m.topic=co.topic; m.tag="perf192.ack.batch.v1";
            m.body="exact-body:"+std::to_string(i);
            body_keys.emplace(m.body,m.message_key);
            auto sent=producer.Publish(m);
            Require(sent.Published(), "owned publish failed");
            Require(expected.emplace(sent.broker_message_id,m.body).second,"unique broker ID required");
        }
        std::cout<<"SDK192_PUBLISH_TERMINAL cohort="<<cohort<<" sent="<<expected.size()<<std::endl;
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
                Require(expected.count(event.broker_message_id)==1,"foreign event in isolated group");
                Require(event.topic==co.topic && event.tag=="perf192.ack.batch.v1" &&
                    event.body==expected.at(event.broker_message_id),"transport payload changed");
                Require(body_keys.count(event.body)==1 &&
                    std::find(event.keys.begin(),event.keys.end(),body_keys.at(event.body))!=event.keys.end(),
                    "event key does not match exact published body");
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
        ROCKETMQ_NAMESPACE::getLogger().setLevel(ROCKETMQ_NAMESPACE::Level::Debug);
        const auto debug_probe=consumer.Receive(1,10000);
        Require(debug_probe.success,"DEBUG receive probe failed");
        Require(debug_probe.events.empty(),"previously acknowledged message returned during DEBUG probe");
        ROCKETMQ_NAMESPACE::getLogger().setLevel(ROCKETMQ_NAMESPACE::Level::Info);
        std::cout<<"SDK192_DEBUG_RECEIVE_COMPLETED"<<std::endl;
        consumer.Stop(); // Only after AckBatch has joined all callbacks.
        auto stopped=consumer.AckBatch(std::vector<ConsumedEvent>(2));
        Require(stopped.size()==2 && !stopped[0].success && !stopped[1].success,
                "stopped transport cannot report ACK success");
        producer.Stop();
        std::cout<<"[PASS] real MQ:32 exact bodies; bounded multi-event async ACK; one missing receipt remains unacked; real redelivery recovered; stopped transport fails; max_batch="<<maximum_batch<<'\n';
        std::cout<<"SDK192_REAL_BROKER_RECEIPT_REDELIVERY_STOP_PASS cohort="<<cohort<<" lazy="<<lazy<<" records="<<completed.size()<<std::endl;
        tinyimx::Logger::Instance().Shutdown();
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"[FAIL] "<<e.what()<<'\n';
        producer.Stop(); consumer.Stop(); tinyimx::Logger::Instance().Shutdown(); return 1;
    }
}
