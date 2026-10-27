#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "services/eventing/rocketmq/RocketMQSimpleConsumer.h"
#include <rocketmq/Configuration.h>
#include <rocketmq/CredentialsProvider.h>
#include <rocketmq/ErrorCode.h>
#include <rocketmq/FilterExpression.h>
#include <rocketmq/Logger.h>
#include <rocketmq/Message.h>
#include <rocketmq/SimpleConsumer.h>
#include <rocketmq/TinyIMXBatchAckApi.h>
#include <dlfcn.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace mq=ROCKETMQ_NAMESPACE;
namespace {
void Require(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
std::vector<mq::MessageConstSharedPtr> Make(const std::string&mode,unsigned count,const std::string&topic,const std::string&endpoint){
 std::vector<mq::MessageConstSharedPtr> messages;
 for(unsigned i=0;i<count;++i){auto built=mq::Message::newBuilder().withTopic(topic).withBody("owned-wire-no-publish").build();
  auto message=std::make_shared<mq::Message>(*built);message->mutableExtension().receipt_handle=mode+":"+std::to_string(i);message->mutableExtension().target_endpoint=endpoint;messages.push_back(message);}
 return messages;
}
}
int main(int argc,char**argv){try{

 if(argc==4&&std::string(argv[1])=="--inspect-only"){
  tinyimx::Config cfg;Require(cfg.LoadFromFile(argv[2]),"inspect config rejected");
  Require(cfg.RocketMQ().message_topic=="codex_perf192_events_20261021_r3_off1"&&cfg.UnreadProjection().consumer_group=="codex_perf192_consumer_20261021_r3_off1","inspect unowned scope");
  tinyimx::eventing::rocketmq_transport::RocketMQSimpleConsumerOptions options;
  options.endpoint=cfg.RocketMQ().endpoint;options.topic=cfg.RocketMQ().message_topic;options.consumer_group=cfg.UnreadProjection().consumer_group;
  options.tls=cfg.RocketMQ().tls;options.access_key=cfg.RocketMQ().access_key;options.access_secret=cfg.RocketMQ().access_secret;
  Require(tinyimx::Logger::Instance().Init(cfg.Logger()),"inspect logger failed");
  mq::getLogger().setLevel(mq::Level::Warn);mq::getLogger().setConsoleLevel(mq::Level::Warn);
  tinyimx::eventing::rocketmq_transport::RocketMQSimpleConsumer c(options);
  const auto empty=c.ReadWireAckStats();
  Require(!empty.started&&!empty.wire_batch_enabled&&!empty.capability_available&&!empty.snapshot_available&&empty.api_calls==0&&empty.rpc_submissions==0&&empty.submitted_entries==0&&empty.rpc_terminals==0,"not-started stats");
  const std::string mode=argv[3];Require(mode=="old"||mode=="off"||mode=="on","unknown inspect mode");
  Require(c.Start(),"inspect start failed");const auto a=c.ReadWireAckStats();
  Require(a.started&&a.wire_batch_enabled==(mode=="on")&&a.capability_available==(mode!="old")&&a.snapshot_available==(mode!="old"),"capability/enabled/snapshot conflated");
  Require(a.api_calls==0&&a.rpc_submissions==0&&a.submitted_entries==0&&a.rpc_terminals==0,"fresh no-API-use stats nonzero");
  c.Stop();const auto stopped=c.ReadWireAckStats();Require(!stopped.started&&!stopped.capability_available&&!stopped.snapshot_available&&!stopped.wire_batch_enabled&&stopped.api_calls==0&&stopped.rpc_submissions==0&&stopped.submitted_entries==0&&stopped.rpc_terminals==0,"stopped snapshot misleading");
  tinyimx::Logger::Instance().Shutdown();
  std::cout<<"MAIN248_INSPECT_PASS mode="<<mode<<" capability="<<a.capability_available<<" snapshot="<<a.snapshot_available<<" enabled="<<a.wire_batch_enabled<<" new_api_entries="<<a.submitted_entries<<std::endl;return 0;
 }
 Require(argc==5,"config mock1 mock2 candidate");tinyimx::Config cfg;Require(cfg.LoadFromFile(argv[1]),"config rejected");
 Require(cfg.RocketMQ().message_topic=="codex_perf192_events_20261021_r3_off1"&&cfg.UnreadProjection().consumer_group=="codex_perf192_consumer_20261021_r3_off1","unowned scope");
 using Getter=const mq::TinyIMXBatchAckApiV1*(*)();auto getter=reinterpret_cast<Getter>(dlsym(RTLD_DEFAULT,"tinyimx_rocketmq_batch_ack_api_v1"));Require(getter,"batch capability missing");Dl_info info{};Require(dladdr(reinterpret_cast<void*>(getter),&info)&&std::filesystem::canonical(info.dli_fname)==std::filesystem::canonical(argv[4]),"wrong SDK loaded");
 auto*api=getter();Require(api->magic==mq::kTinyIMXBatchAckMagic&&api->version==1&&api->structure_bytes==sizeof(*api)&&api->gnu_cxx11_abi==_GLIBCXX_USE_CXX11_ABI&&api->maximum_entries==256&&api->consumer_bytes==sizeof(mq::SimpleConsumer)&&api->message_pointer_bytes==sizeof(mq::MessageConstSharedPtr)&&api->ack_batch&&api->read_stats,"capability ABI differs");
 mq::getLogger().setLevel(mq::Level::Warn);mq::getLogger().setConsoleLevel(mq::Level::Warn);
 tinyimx::eventing::rocketmq_transport::RocketMQSimpleConsumerOptions options;
 options.endpoint=cfg.RocketMQ().endpoint;options.topic=cfg.RocketMQ().message_topic;options.consumer_group=cfg.UnreadProjection().consumer_group;
 options.tls=cfg.RocketMQ().tls;options.access_key=cfg.RocketMQ().access_key;options.access_secret=cfg.RocketMQ().access_secret;options.request_timeout_ms=4000;options.await_duration_ms=5000;
 Require(tinyimx::Logger::Instance().Init(cfg.Logger()),"logger failed");
 tinyimx::eventing::rocketmq_transport::RocketMQSimpleConsumer consumer(options);Require(consumer.Start(),"Main consumer start failed");

 auto check_stats=[&](const mq::TinyIMXBatchAckStatsV1& raw){
  const auto actual=consumer.ReadWireAckStats();
  Require(actual.started&&actual.wire_batch_enabled&&actual.capability_available&&actual.snapshot_available,"active stats flags");
  Require(actual.api_calls==raw.api_calls&&actual.rpc_submissions==raw.rpc_submissions&&actual.submitted_entries==raw.submitted_entries&&actual.rpc_terminals==raw.rpc_terminals,"Main snapshot differs from actual SDK counter");
 };
 unsigned cases=0;
 auto call=[&](const std::string&name,const std::vector<mq::MessageConstSharedPtr>&messages,const std::vector<bool>&success,unsigned rpc,unsigned entries,int exact_error=0){
  mq::TinyIMXBatchAckStatsV1 before{},after{};api->read_stats(&before);check_stats(before);
  std::vector<tinyimx::eventing::ConsumedEvent> events;for(const auto&message:messages){tinyimx::eventing::ConsumedEvent e;e.native_handle=std::static_pointer_cast<const void>(message);events.push_back(std::move(e));}
  const auto start=std::chrono::steady_clock::now();auto results=consumer.AckBatch(events);const auto wall=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();api->read_stats(&after);check_stats(after);
  Require(results.size()==messages.size()&&success.size()==messages.size(),"Main result size differs");
  for(std::size_t i=0;i<results.size();++i){Require(results[i].success==success[i],"Main per-entry success mapped incorrectly");Require(!results[i].message.empty(),"Main error/result explanation missing");}(void)exact_error;
  Require(after.rpc_submissions-before.rpc_submissions==rpc&&after.submitted_entries-before.submitted_entries==entries&&after.rpc_terminals-before.rpc_terminals==rpc,"submission/entry/terminal counts differ");
  if(name=="timeout")Require(wall>=3000&&wall<6000,"shared four-second network deadline multiplied or returned early");
  std::cout<<"MAIN248_CASE {\"case\":\""<<name<<"\",\"inputs\":"<<messages.size()<<",\"expected_rpc\":"<<rpc<<",\"sdk_submissions\":"<<after.rpc_submissions-before.rpc_submissions<<",\"entries\":"<<entries<<",\"wall_ms\":"<<wall<<",\"success\":[";
  for(std::size_t i=0;i<results.size();++i){if(i)std::cout<<',';std::cout<<(results[i].success?1:0);}std::cout<<"]}"<<std::endl;++cases;
 };
 const std::string topic=cfg.RocketMQ().message_topic,a=argv[2],b=argv[3];
 call("empty",{},{},0,0);call("nulls",std::vector<mq::MessageConstSharedPtr>(8),std::vector<bool>(8,false),0,0);
 call("over",std::vector<mq::MessageConstSharedPtr>(257),std::vector<bool>(257,false),0,0);
 for(const std::string mode:{"ok","reverse","canonical"})call(mode,Make(mode,16,topic,a),std::vector<bool>(16,true),1,16);
 call("max",Make("max",256,topic,a),std::vector<bool>(256,true),1,256);
 auto mixed=std::vector<bool>(8,true);mixed[0]=false;call("mixed",Make("mixed",8,topic,a),mixed,1,8);
 for(const std::string mode:{"missing","duplicate","foreign","inconsistent"})call(mode,Make(mode,8,topic,a),std::vector<bool>(8,false),1,8,static_cast<int>(std::errc::protocol_error));
 call("unauthorized",Make("unauthorized",8,topic,a),std::vector<bool>(8,false),1,8,static_cast<int>(mq::ErrorCode::Unauthorized));
 call("grpcforbidden",Make("grpcforbidden",8,topic,a),std::vector<bool>(8,false),1,8,static_cast<int>(mq::ErrorCode::Forbidden));
 auto duplicate=Make("duplicateinput",1,topic,a);duplicate.push_back(duplicate[0]);call("duplicateinput",duplicate,std::vector<bool>(2,true),2,2);
 auto split=Make("split",4,topic,a),tail=Make("split",4,topic,b);split.insert(split.end(),tail.begin(),tail.end());call("split",split,std::vector<bool>(8,true),2,8);
 auto nullslot=Make("nullslot",8,topic,a);nullslot[3].reset();auto expected=std::vector<bool>(8,true);expected[3]=false;call("nullslot",nullslot,expected,1,7);
 auto timeout=Make("timeout",4,topic,a),last=Make("timeout",4,topic,b);timeout.insert(timeout.end(),last.begin(),last.end());call("timeout",timeout,std::vector<bool>(8,false),2,8,static_cast<int>(mq::ErrorCode::RequestTimeout));

 mq::TinyIMXBatchAckStatsV1 final{};api->read_stats(&final);check_stats(final);
 Require(final.rpc_submissions==18&&final.submitted_entries==385&&final.rpc_terminals==18,"quiescent actual totals");
 std::cout<<"MAIN248_FINAL_COUNTERS api_calls="<<final.api_calls<<" rpc_submissions="<<final.rpc_submissions<<" entries="<<final.submitted_entries<<" terminals="<<final.rpc_terminals<<std::endl;
 consumer.Stop();const auto inactive=consumer.ReadWireAckStats();Require(!inactive.started&&!inactive.capability_available&&!inactive.snapshot_available&&inactive.rpc_submissions==0,"stopped stats available");auto stopped=consumer.AckBatch(std::vector<tinyimx::eventing::ConsumedEvent>(2));Require(stopped.size()==2&&!stopped[0].success&&!stopped[1].success,"stopped Main falsely successful");tinyimx::Logger::Instance().Shutdown();
 std::cout<<"MAIN248_REAL_GRPC_BATCH_CASES_PASS cases="<<cases<<" no_publish_or_receive=true"<<std::endl;return 0;
}catch(const std::exception&e){std::cerr<<"MAIN248_FAIL "<<e.what()<<std::endl;return 1;}}
