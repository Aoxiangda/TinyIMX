// Real public SDK/RPC test: delay a callback in an owned client, never a service.
#include <rocketmq/Configuration.h>
#include <rocketmq/ErrorCode.h>
#include <rocketmq/Logger.h>
#include <rocketmq/Message.h>
#include <rocketmq/Producer.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
namespace mq=ROCKETMQ_NAMESPACE;
using Clock=std::chrono::steady_clock;
using namespace std::chrono;
namespace {
struct Gate {
 std::mutex mutex;std::condition_variable ready;
 bool entered=false,released=false,done=false,failed=false;
 std::string receipt;std::thread::id callback_thread;
};
struct Release {
 std::shared_ptr<Gate> state;
 ~Release(){std::lock_guard<std::mutex> lock(state->mutex);state->released=true;state->ready.notify_all();}
};
void Check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
mq::MessageConstPtr Message(const std::string& topic,const std::string& label,const std::string& suffix){
 const auto body="PERF801:"+label+":"+suffix;
 return mq::Message::newBuilder().withTopic(topic).withTag("sdk.lifecycle").withKeys(std::vector<std::string>{body}).withBody(body).build();
}
long long WallMS(){return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();}
struct Returned {int code;long long before_ms,after_ms,elapsed_us;};
#if defined(__GNUC__)
__attribute__((noinline))
#endif
Returned TimeoutCaller(mq::Producer& producer,const std::string& topic,const std::string& label){
 std::error_code error;
 const auto begin=Clock::now();const auto wall=WallMS();
 auto receipt=producer.send(Message(topic,label,"target"),error);
 const auto after=WallMS();const auto elapsed=duration_cast<microseconds>(Clock::now()-begin).count();
 // Copy out plain values only. Both error and receipt cease to exist on return.
 Check(error==mq::ErrorCode::RequestTimeout,"expected real synchronous deadline while callback thread occupied");
 Check(receipt.message_id.empty(),"timeout cannot present a successful receipt");
 return {error.value(),wall,after,elapsed};
}
}
int main(int argc,char**argv)try{
 if(argc!=4)return 2;
 const std::string endpoint=argv[1],topic=argv[2],label=argv[3];
 Check(std::regex_match(topic,std::regex("codex_perf801_events_20261024_r1_(pilot|unsafe|safe1|safe2)")),"owned topic required");
 Check(std::regex_match(label,std::regex("(pilot|unsafe|safe1|safe2)")),"owned case required");
 mq::getLogger().setLevel(mq::Level::Error);mq::getLogger().setConsoleLevel(mq::Level::Error);
 auto config=mq::Configuration::newBuilder().withEndpoints(endpoint).withSsl(false)
  .withRequestTimeout(milliseconds(1500)).withCallbackThreads(1).build();
 auto producer=mq::Producer::newBuilder().withConfiguration(config).withTopics(std::vector<std::string>{topic}).build();
 auto gate=std::make_shared<Gate>();Release release{gate};
 const auto main_thread=std::this_thread::get_id();
 producer.send(Message(topic,label,"blocker"),[gate](const std::error_code& ec,mq::SendReceipt&& receipt){
  std::unique_lock<std::mutex> lock(gate->mutex);
  gate->callback_thread=std::this_thread::get_id();gate->entered=true;
  gate->failed=bool(ec)||receipt.message_id.empty();gate->receipt=receipt.message_id;
  gate->ready.notify_all();
  if(!gate->failed)gate->ready.wait(lock,[gate]{return gate->released;});
  gate->done=true;gate->ready.notify_all();
 });
 {
  std::unique_lock<std::mutex> lock(gate->mutex);
  Check(gate->ready.wait_for(lock,seconds(15),[gate]{return gate->entered;}),"first actual RPC callback missing");
  Check(!gate->failed&&gate->callback_thread!=main_thread,"first actual RPC must succeed on SDK callback worker");
 }
 std::cout<<"RPC801_CALLBACK_HELD "<<WallMS()<<std::endl;
 auto returned=TimeoutCaller(producer,topic,label);
 Check(returned.elapsed_us>=1400000&&returned.elapsed_us<=6000000,"bounded actual deadline duration");
 {
  std::lock_guard<std::mutex> lock(gate->mutex);Check(!gate->done,"callback worker must still be held");
 }
 std::cout<<"RPC801_STACK_RETURNED {\"code\":"<<returned.code<<",\"before_ms\":"<<returned.before_ms
  <<",\"after_ms\":"<<returned.after_ms<<",\"elapsed_us\":"<<returned.elapsed_us<<"}"<<std::endl;
 // The production sync frame and caller's error/receipt are now destroyed.
 // Release the SDK worker; an old reference-capturing completion is unsafe.
 {
  std::lock_guard<std::mutex> lock(gate->mutex);gate->released=true;gate->ready.notify_all();
 }
 {
  std::unique_lock<std::mutex> lock(gate->mutex);
  Check(gate->ready.wait_for(lock,seconds(10),[gate]{return gate->done;}),"held callback did not return");
 }
 std::this_thread::sleep_for(milliseconds(250));
 std::error_code error;auto final=producer.send(Message(topic,label,"after"),error);
 Check(!error&&!final.message_id.empty(),"producer must still publish normally after a late completion");
 Check(returned.code==static_cast<int>(mq::ErrorCode::RequestTimeout),"copied timeout result stable");
 std::cout<<"SDK801_ACTUAL_RPC_LATE_CALLBACK_RECOVERED "<<label<<std::endl;
 return 0;
}catch(const std::exception& ex){std::cerr<<"SDK801_TEST_FAILED "<<ex.what()<<std::endl;return 1;}
