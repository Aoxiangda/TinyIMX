// Finite Broker correctness test for the real Main transport, not a benchmark.
#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "services/eventing/rocketmq/RocketMQProducer.h"
#include "services/eventing/rocketmq/RocketMQSimpleConsumer.h"
#include "services/eventing/rocketmq/OptionalWireAckBatch.h"
#include <rocketmq/Logger.h>
#include <dlfcn.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <thread>

namespace mq=ROCKETMQ_NAMESPACE;
using namespace tinyimx::eventing;
using namespace tinyimx::eventing::rocketmq_transport;
using namespace std::chrono_literals;
void Require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}

int main(int argc,char**argv)try{
 if(argc!=6||std::string(argv[2])!="--owned-main235")return 2;
 const std::string cohort=argv[3],mode=argv[5];
 if(cohort!="off1"&&cohort!="on1"&&cohort!="on2"&&cohort!="off2")return 2;
 if(mode!="--validate"&&mode!="--run"&&mode!="--probe-missing"&&mode!="--probe-invalid")return 2;
 tinyimx::Config cfg;
 if(!cfg.LoadFromFile(argv[1])){std::cerr<<"MAIN235_CONFIG_REJECTED "<<cfg.LastError()<<'\n';return 2;}
 if(cfg.RocketMQ().message_topic!="codex_perf235_events_20261021_r4_"+cohort||cfg.UnreadProjection().consumer_group!="codex_perf235_consumer_20261021_r4_"+cohort)return 2;
 if(mode=="--validate"){std::cout<<"MAIN235_CONFIG_VALIDATED_NO_CLIENT\n";return 0;}
 void* symbol=dlsym(RTLD_DEFAULT,"_ZN8rocketmq9getLoggerEv");Dl_info info{};
 Require(symbol&&dladdr(symbol,&info)&&info.dli_fname&&std::filesystem::canonical(info.dli_fname)==std::filesystem::canonical(argv[4]),"wrong SDK loaded");
 mq::getLogger().setLevel(mq::Level::Info);mq::getLogger().setConsoleLevel(mq::Level::Warn);
 Require(tinyimx::Logger::Instance().Init(cfg.Logger()),"app logger failed");
 RocketMQSimpleConsumerOptions co;
 co.endpoint=cfg.RocketMQ().endpoint;co.topic=cfg.RocketMQ().message_topic;co.consumer_group=cfg.UnreadProjection().consumer_group;
 co.request_timeout_ms=cfg.RocketMQ().request_timeout_ms;co.await_duration_ms=cfg.UnreadProjection().await_duration_ms;
 co.tls=cfg.RocketMQ().tls;co.access_key=cfg.RocketMQ().access_key;co.access_secret=cfg.RocketMQ().access_secret;
 RocketMQSimpleConsumer consumer(co);
 if(mode=="--probe-missing"||mode=="--probe-invalid"){
  Require(!consumer.Start()&&!consumer.IsStarted(),"unavailable capability or invalid flag started client");
  Require(consumer.LastError().find(mode=="--probe-missing"?"capability is unavailable":"must be 0 or 1")!=std::string::npos,"startup rejection classification wrong");
  std::cout<<"MAIN235_STARTUP_REJECTION_PASS mode="<<mode<<" no_client=true\n";tinyimx::Logger::Instance().Shutdown();return 0;
 }
 const bool wire=true;
 const char* flag=std::getenv("TINYIMX_ROCKETMQ_WIRE_ACK_BATCH_ENABLE");Require(flag&&std::string(flag)==(wire?"1":"0"),"wire mode differs");
 using Getter=const mq::TinyIMXBatchAckApiV1*(*)()noexcept;
 const auto getter=reinterpret_cast<Getter>(dlsym(RTLD_DEFAULT,"tinyimx_rocketmq_batch_ack_api_v1"));
 Require(getter&&detail::CompatibleWireAckApi(getter()),"SDK capability missing");const auto*api=getter();
 RocketMQProducerOptions po;po.endpoint=co.endpoint;po.topic=co.topic;po.request_timeout_ms=co.request_timeout_ms;po.tls=co.tls;po.access_key=co.access_key;po.access_secret=co.access_secret;
 RocketMQProducer producer(po);
 Require(consumer.Start(),"consumer start failed");Require(producer.Start(),"producer start failed");
 mq::TinyIMXBatchAckStatsV1 first{},last{};api->read_stats(&first);
 Require(consumer.AckBatch({}).empty(),"empty batch failed");
 auto over=consumer.AckBatch(std::vector<ConsumedEvent>(257));Require(over.size()==257&&std::none_of(over.begin(),over.end(),[](const auto&v){return v.success;}),"oversized batch accepted");
 std::map<std::string,std::string> expected,keys;
 for(unsigned i=0;i<32;++i){
  PublishMessage m;m.event_id="perf235-"+cohort+"-"+std::to_string(i);m.message_key=m.event_id;m.topic=co.topic;m.tag="perf235.ack.batch.v1";m.body="exact-body:"+std::to_string(i);keys[m.body]=m.message_key;
  auto sent=producer.Publish(m);Require(sent.Published(),"owned publish failed");Require(expected.emplace(sent.broker_message_id,m.body).second,"duplicate broker ID");
 }
 std::cout<<"MAIN235_PUBLISH_TERMINAL cohort="<<cohort<<" sent="<<expected.size()<<std::endl;
 std::map<std::string,unsigned> complete;std::string unacked;bool redelivered=false,merged=false;std::size_t max_batch=0,valid_total=0,batches=0;
 const auto deadline=std::chrono::steady_clock::now()+100s;
 while(std::chrono::steady_clock::now()<deadline&&complete.size()<32){
  auto received=consumer.Receive(16,10000);Require(received.success,"owned receive failed");if(received.events.empty())continue;
  max_batch=std::max(max_batch,received.events.size());
  for(const auto&e:received.events){Require(expected.count(e.broker_message_id)==1&&e.body==expected.at(e.broker_message_id),"foreign or changed body");Require(e.topic==co.topic&&e.tag=="perf235.ack.batch.v1"&&std::find(e.keys.begin(),e.keys.end(),keys.at(e.body))!=e.keys.end(),"topic/tag/key changed");Require(!complete.count(e.broker_message_id),"ACKed event redelivered");}
  const bool inject=unacked.empty();if(inject){unacked=received.events.front().broker_message_id;received.events.front().native_handle.reset();}
  mq::TinyIMXBatchAckStatsV1 before{},after{};api->read_stats(&before);auto result=consumer.AckBatch(received.events);api->read_stats(&after);
  const auto valid=received.events.size()-(inject?1:0);valid_total+=valid;++batches;
  Require(result.size()==received.events.size(),"ACK result positions lost");
  if(wire){Require(after.submitted_entries-before.submitted_entries==valid,"wire ACK did not submit each valid entry exactly once");Require(after.rpc_submissions-before.rpc_submissions==after.rpc_terminals-before.rpc_terminals,"returned before all RPC terminals");if(valid>1&&after.rpc_submissions-before.rpc_submissions<valid)merged=true;}
  else Require(after.api_calls==before.api_calls&&after.rpc_submissions==before.rpc_submissions,"OFF used batch SDK API");
  for(std::size_t i=0;i<result.size();++i){const auto&id=received.events[i].broker_message_id;if(inject&&i==0){Require(!result[i].success,"null handle falsely confirmed");continue;}Require(result[i].success,"valid receipt ACK failed");++complete[id];if(id==unacked)redelivered=true;}
 }
 Require(complete.size()==32&&redelivered&&max_batch>=2,"real unacked message did not recover");Require(!wire||merged,"no real multi-entry request formed");
 // Wait beyond the ten-second invisibility period before checking ACKed data.
 std::this_thread::sleep_for(11s);auto empty=consumer.Receive(1,10000);Require(empty.success&&empty.events.empty(),"ACKed data returned after invisibility window");
 api->read_stats(&last);Require(!wire||last.submitted_entries-first.submitted_entries==32,"confirmed entries total differs");
 consumer.Stop();auto stopped=consumer.AckBatch(std::vector<ConsumedEvent>(2));Require(stopped.size()==2&&!stopped[0].success&&!stopped[1].success,"stopped consumer reported success");producer.Stop();
 std::cout<<"MAIN235_REAL_BROKER_PASS {\"cohort\":\""<<cohort<<"\",\"wire\":"<<wire<<",\"completed\":"<<complete.size()<<",\"redelivered\":"<<redelivered<<",\"max_batch\":"<<max_batch<<",\"batch_calls\":"<<batches<<",\"valid_entries\":"<<valid_total<<",\"sdk_rpc_submissions\":"<<last.rpc_submissions-first.rpc_submissions<<",\"sdk_entries\":"<<last.submitted_entries-first.submitted_entries<<",\"merged\":"<<merged<<"}"<<std::endl;
 tinyimx::Logger::Instance().Shutdown();return 0;
}catch(const std::exception&e){std::cerr<<"MAIN235_FAIL "<<e.what()<<std::endl;tinyimx::Logger::Instance().Shutdown();return 1;}
