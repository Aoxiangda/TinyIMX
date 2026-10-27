#include "common/config/Config.h"
#include "services/eventing/rocketmq/RocketMQSimpleConsumer.h"
#include <iostream>
#include <string>
int main(int argc,char**argv) {
    if(argc!=4)return 2;
    tinyimx::Config config;
    if(!config.LoadFromFile(argv[1])||config.RocketMQ().message_topic!="codex_perf041_events_20261019_r27")return 2;
    tinyimx::eventing::rocketmq_transport::RocketMQSimpleConsumerOptions o;
    o.endpoint=config.RocketMQ().endpoint;o.topic=config.RocketMQ().message_topic;
    o.consumer_group=config.UnreadProjection().consumer_group;o.request_timeout_ms=config.RocketMQ().request_timeout_ms;
    o.await_duration_ms=std::stoi(argv[2]);o.tls=config.RocketMQ().tls;
    o.access_key=config.RocketMQ().access_key;o.access_secret=config.RocketMQ().access_secret;
    if((o.await_duration_ms!=1000&&o.await_duration_ms!=5000)||
       (std::string(argv[3])!="10000"&&std::string(argv[3])!="30000"))return 2;
    tinyimx::eventing::rocketmq_transport::RocketMQSimpleConsumer consumer(o);
    if(!consumer.Start()){std::cout<<"START_FAILED "<<consumer.LastError()<<'\n';return 2;}
    auto result=consumer.Receive(16,std::stoi(argv[3]));
    std::cout<<"AWAIT="<<o.await_duration_ms<<" INVISIBLE="<<argv[3]
             <<" SUCCESS="<<result.success<<" COUNT="<<result.events.size()<<" MESSAGE="<<result.message<<'\n';
    // Diagnostic only: no publishing, ACK, offset reset or mutation of records.
    consumer.Stop();return 0;
}
