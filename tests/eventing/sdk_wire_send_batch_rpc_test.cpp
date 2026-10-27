// Finite owned-broker contract test; production services are never faulted.
#include <rocketmq/Configuration.h>
#include <rocketmq/ErrorCode.h>
#include <rocketmq/Logger.h>
#include <rocketmq/Message.h>
#include <rocketmq/Producer.h>
#include <rocketmq/TinyIMXBatchSendApi.h>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
namespace mq=ROCKETMQ_NAMESPACE;
using namespace std::chrono;
namespace {
void Check(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
struct Record{std::string id,body,tag,key,group;unsigned ordinal;};
std::vector<Record> records;
mq::MessageConstSharedPtr Message(const std::string& topic,const std::string& group,
                                  unsigned ordinal,const std::string& tag="perf1058.sdk.batch.v1"){
 const auto body="PERF1058:"+group+":"+std::to_string(ordinal);
 auto message=mq::Message::newBuilder().withTopic(topic).withTag(tag)
  .withKeys(std::vector<std::string>{body}).withBody(body).build();
 return mq::MessageConstSharedPtr(std::move(message));
}
mq::MessageConstPtr LegacyMessage(const std::string& topic,const std::string& group){
 const auto body="PERF1058:"+group+":0";
 return mq::Message::newBuilder().withTopic(topic).withTag("perf1058.sdk.batch.v1")
  .withKeys(std::vector<std::string>{body}).withBody(body).build();
}
void Remember(const mq::Message& m,const std::string& group,unsigned ordinal){
 records.push_back({m.id(),std::string(m.body()),m.tag(),m.keys().front(),group,ordinal});
}
void PositiveBatch(mq::Producer& producer,const std::string& topic,unsigned count){
 const auto group="normal"+std::to_string(count);
 std::vector<mq::MessageConstSharedPtr> messages;
 for(unsigned i=0;i<count;++i){messages.push_back(Message(topic,group,i));Remember(*messages.back(),group,i);}
 std::vector<std::error_code> errors;std::vector<std::string> ids;
 producer.sendBatch(messages,errors,ids);
 Check(errors.size()==count&&ids.size()==count,"positive batch result length");
 for(unsigned i=0;i<count;++i)Check(!errors[i]&&ids[i]==messages[i]->id(),"every receipt must match its request ID");
}
void RejectedBatch(mq::Producer& producer,const std::vector<mq::MessageConstSharedPtr>& messages){
 auto api=mq::tinyimx_rocketmq_batch_send_api_v1();mq::TinyIMXBatchSendStatsV1 before{},after{};api->read_stats(&before);
 std::vector<std::error_code> errors;std::vector<std::string> ids;producer.sendBatch(messages,errors,ids);
 Check(errors.size()==messages.size()&&ids.size()==messages.size(),"rejected result preserves input shape");
 for(unsigned i=0;i<messages.size();++i)Check(bool(errors[i])&&ids[i].empty(),"invalid entry cannot look successful");
 api->read_stats(&after);Check(before.rpc_submissions==after.rpc_submissions,"invalid batch cannot submit a prefix");
}
struct Gate{std::mutex mutex;std::condition_variable cv;bool entered=false,released=false,done=false,failed=false;};
struct Release{std::shared_ptr<Gate> gate;~Release(){std::lock_guard<std::mutex> lock(gate->mutex);gate->released=true;gate->cv.notify_all();}};
struct Returned{long long elapsed_us,returned_wall_ms;};
#if defined(__GNUC__)
__attribute__((noinline))
#endif
Returned BatchTimeoutFrame(mq::Producer& producer,const std::string& topic){
 std::vector<mq::MessageConstSharedPtr> messages;
 for(unsigned i=0;i<2;++i){messages.push_back(Message(topic,"deadline",i));Remember(*messages.back(),"deadline",i);}
 std::vector<std::error_code> errors;std::vector<std::string> ids;
 auto start=steady_clock::now();producer.sendBatch(messages,errors,ids);
 auto elapsed=duration_cast<microseconds>(steady_clock::now()-start).count();
 Check(errors.size()==2&&ids.size()==2,"deadline result length");
 for(unsigned i=0;i<2;++i)Check(errors[i]==mq::ErrorCode::RequestTimeout&&ids[i].empty(),"unknown deadline cannot confirm any entry");
 Check(elapsed>=1400000&&elapsed<=6000000,"bounded actual deadline");
 return{elapsed,duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count()};
 // messages/errors/ids are destroyed before the late callback is released.
}
void Save(const std::string& destination,const Returned& timed,const mq::TinyIMXBatchSendStatsV1& stats){
 std::ofstream out(destination,std::ios::out|std::ios::trunc);Check(bool(out),"open owned proof file");
 // All strings come from controlled ASCII bodies or the SDK's generated IDs.
 out<<"{\"status\":\"ACTUAL_SDK_BATCH2_16_INVALID_PREFIX_DEADLINE_LATE_CALLBACK_AND_LEGACY_SEND_PASS\",\"timeout_us\":"<<timed.elapsed_us
 <<",\"timeout_returned_wall_ms\":"<<timed.returned_wall_ms<<",\"rpc_submissions\":"<<stats.rpc_submissions
 <<",\"submitted_entries\":"<<stats.submitted_entries<<",\"rpc_terminals\":"<<stats.rpc_terminals
 <<",\"synchronous_timeouts\":"<<stats.synchronous_timeouts<<",\"expected\":[";
 bool first=true;for(const auto& r:records){if(!first)out<<',';first=false;
  out<<"{\"id\":\""<<r.id<<"\",\"body\":\""<<r.body<<"\",\"tag\":\""<<r.tag<<"\",\"key\":\""<<r.key<<"\",\"group\":\""<<r.group<<"\",\"ordinal\":"<<r.ordinal<<'}';}
 out<<"]}\n";out.flush();Check(bool(out),"flush owned proof file");
}
}
int main(int argc,char**argv)try{
 if(argc!=4||std::string(argv[2])!="codex_perf1058_events_20261025_r1")return 2;
 const std::string endpoint=argv[1],topic=argv[2];
 mq::getLogger().setLevel(mq::Level::Error);mq::getLogger().setConsoleLevel(mq::Level::Error);
 auto config=mq::Configuration::newBuilder().withEndpoints(endpoint).withSsl(false)
  .withRequestTimeout(milliseconds(1500)).withCallbackThreads(1).build();
 auto producer=mq::Producer::newBuilder().withConfiguration(config).withTopics(std::vector<std::string>{topic}).build();
 auto api=mq::tinyimx_rocketmq_batch_send_api_v1();Check(api&&api->version==1&&api->maximum_entries==32,"actual optional API present");
 std::vector<mq::MessageConstSharedPtr> empty;std::vector<std::error_code> errors;std::vector<std::string>ids;
 producer.sendBatch(empty,errors,ids);Check(errors.empty()&&ids.empty(),"empty batch");
 auto one=Message(topic,"rejected",0);
 RejectedBatch(producer,std::vector<mq::MessageConstSharedPtr>(33,one));
 RejectedBatch(producer,{one,one});
 RejectedBatch(producer,{one,mq::MessageConstSharedPtr{}});
 RejectedBatch(producer,{one,Message(topic,"rejected",1,"different.tag")});
 auto large=mq::Message::newBuilder().withTopic(topic).withTag("perf1058.sdk.batch.v1").withBody(std::string(256*1024+1,'x')).build();
 RejectedBatch(producer,{mq::MessageConstSharedPtr(std::move(large))});
 PositiveBatch(producer,topic,2);PositiveBatch(producer,topic,16);
 auto gate=std::make_shared<Gate>();Release release{gate};auto blocker=LegacyMessage(topic,"blocker");Remember(*blocker,"blocker",0);
 const auto main_thread=std::this_thread::get_id();
 producer.send(std::move(blocker),[gate,main_thread](const std::error_code& ec,mq::SendReceipt&& receipt){
  std::unique_lock<std::mutex> lock(gate->mutex);gate->entered=true;
  gate->failed=bool(ec)||receipt.message_id.empty()||std::this_thread::get_id()==main_thread;gate->cv.notify_all();
  if(!gate->failed&&!gate->cv.wait_for(lock,seconds(8),[gate]{return gate->released;}))gate->failed=true;
  gate->done=true;gate->cv.notify_all();
 });
 {std::unique_lock<std::mutex> lock(gate->mutex);Check(gate->cv.wait_for(lock,seconds(15),[gate]{return gate->entered;})&&!gate->failed,"real legacy callback held on SDK worker");}
 const auto timed=BatchTimeoutFrame(producer,topic);
 {std::lock_guard<std::mutex> lock(gate->mutex);Check(!gate->done,"caller frame destroyed while callback worker is still held");gate->released=true;gate->cv.notify_all();}
 {std::unique_lock<std::mutex> lock(gate->mutex);Check(gate->cv.wait_for(lock,seconds(10),[gate]{return gate->done;})&&!gate->failed,"held callback releases cleanly");}
 mq::TinyIMXBatchSendStatsV1 stats{};const auto deadline=steady_clock::now()+seconds(10);
 do{api->read_stats(&stats);if(stats.rpc_terminals==3)break;std::this_thread::sleep_for(milliseconds(10));}while(steady_clock::now()<deadline);
 Check(stats.rpc_submissions==3&&stats.submitted_entries==20&&stats.rpc_terminals==3&&stats.synchronous_timeouts==1&&!stats.malformed_responses,"exact real RPC batch and late terminal counts");
 auto after=LegacyMessage(topic,"after");Remember(*after,"after",0);std::error_code error;
 auto receipt=producer.send(std::move(after),error);Check(!error&&!receipt.message_id.empty(),"legacy synchronous send works after late batch callback");
 Check(records.size()==22,"all22 synthetic messages recorded");Save(argv[3],timed,stats);
 std::cout<<"ACTUAL_SDK_BATCH22_DEADLINE_LATE_CALLBACK_AND_LEGACY_SEND_PASS\n";return 0;
}catch(const std::exception& ex){std::cerr<<"SDK_BATCH_CONTRACT_FAILED "<<ex.what()<<'\n';return 1;}
