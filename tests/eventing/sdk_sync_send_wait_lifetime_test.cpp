// Compile with the exact old/patched vendor function in SyncSendFunction.inc.
// Only the owning class qualifier changes to Harness; the function body is
// extracted byte-for-byte from the fixed Apache-2.0 SDK source. Dispatch is
// controlled here; Message/SendReceipt/ErrorCode, absl and protobuf are real.
#include "rocketmq/Message.h"
#include "rocketmq/SendReceipt.h"
#include "rocketmq/SendCallback.h"
#include "rocketmq/ErrorCode.h"
#include "Protocol.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "absl/types/optional.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef SPDLOG_WARN
#undef SPDLOG_WARN
#endif
#define SPDLOG_WARN(...) ((void)0)
ROCKETMQ_NAMESPACE_BEGIN
struct Invocation {
  SendCallback callback;
  MessageConstPtr message;
  void Complete(const std::error_code& code={}) {
    SendReceipt receipt;receipt.target="owned-target";receipt.message_id="owned-mid";
    receipt.transaction_id="owned-transaction";receipt.recall_handle="owned-recall";
    receipt.message=std::move(message);callback(code,std::move(receipt));
  }
};
struct Route {
  bool selectable{true};
  bool selectMessageQueues(const absl::optional<std::string>&,std::vector<rmq::MessageQueue>& queues) {
    if(selectable)queues.emplace_back();return selectable;
  }
};
class Harness {
public:
  bool running{true};std::shared_ptr<Route> route{std::make_shared<Route>()};
  std::function<void(std::shared_ptr<Invocation>)> dispatch;
  int timeout_ms{2};
  void ensureRunning(std::error_code& ec) {if(!running)ec=ErrorCode::IllegalState;}
  std::shared_ptr<Route> getPublishInfo(const std::string&) {return route;}
  absl::Duration requestTimeout() {return absl::Milliseconds(timeout_ms);}
  void send0(MessageConstPtr message,SendCallback callback,const std::vector<rmq::MessageQueue>&) {
    auto call=std::make_shared<Invocation>();call->message=std::move(message);call->callback=std::move(callback);dispatch(call);
  }
  SendReceipt send(MessageConstPtr message,std::error_code& ec) noexcept;
};
#include "SyncSendFunction.inc"
ROCKETMQ_NAMESPACE_END
namespace {
using namespace ROCKETMQ_NAMESPACE;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);std::cout<<"PASS "<<why<<std::endl;}
MessageConstPtr MessageForTest(){return Message::newBuilder().withTopic("owned_lifetime_fixture").withBody("owned body").build();}
void CheckReceipt(const SendReceipt& r){Check(r.target=="owned-target"&&r.message_id=="owned-mid"&&r.transaction_id=="owned-transaction"&&r.recall_handle=="owned-recall"&&r.message&&r.message->body()=="owned body","complete owned receipt and message transferred");}
// A separate returning frame lets ASan prove the old function's retained stack
// capture, including when the original caller error_code has also gone away.
__attribute__((noinline)) std::shared_ptr<Invocation> ReturnAfterTimeout() {
  Harness h;std::shared_ptr<Invocation> delayed;h.dispatch=[&](auto call){delayed=call;};std::error_code ec;
  auto result=h.send(MessageForTest(),ec);Check(ec==ErrorCode::RequestTimeout&&result.message_id.empty(),"bounded timeout returned before delayed callback");return delayed;
}
}
int main(int argc,char**argv){
  try{
    if(argc!=2)return 2;const std::string mode=argv[1];
    if(mode=="old-repro") {auto late=ReturnAfterTimeout();late->Complete();std::cout<<"OLD_CALLBACK_RETURNED_WITHOUT_ASAN_DETECTION\n";return 3;}
    if(mode!="contracts")return 2;
    {Harness h;h.dispatch=[](auto call){call->Complete();};std::error_code ec;auto result=h.send(MessageForTest(),ec);Check(!ec,"synchronous inline completion cannot lose wakeup");CheckReceipt(result);}
    {Harness h;h.dispatch=[](auto call){call->Complete(std::make_error_code(std::errc::permission_denied));};std::error_code ec;auto result=h.send(MessageForTest(),ec);Check(ec==std::errc::permission_denied,"server error preserved with owned receipt");CheckReceipt(result);}
    {Harness h;std::shared_ptr<Invocation> late;h.dispatch=[&](auto call){late=call;};std::error_code ec;auto result=h.send(MessageForTest(),ec);const auto saved=ec;Check(saved==ErrorCode::RequestTimeout&&result.message_id.empty(),"no callback before deadline returns unconfirmed timeout");late->Complete();late->Complete();Check(ec==saved&&result.message_id.empty(),"late and duplicate callbacks cannot mutate caller error or returned receipt");}
    {auto late=ReturnAfterTimeout();late->Complete();late->Complete();Check(true,"late callback survives destroyed caller and sender frames");}
    {Harness h;std::shared_ptr<Invocation> retained;h.dispatch=[&](auto call){retained=call;call->Complete();};std::error_code ec;auto result=h.send(MessageForTest(),ec);retained->Complete(std::make_error_code(std::errc::io_error));Check(!ec,"duplicate completion cannot replace completed result");CheckReceipt(result);}
    {Harness h;h.running=false;std::error_code ec;auto result=h.send(MessageForTest(),ec);Check(ec==ErrorCode::IllegalState&&result.message&&result.message->body()=="owned body","not-running retains original pre-dispatch behavior");}
    {Harness h;h.route.reset();std::error_code ec;auto result=h.send(MessageForTest(),ec);Check(ec==ErrorCode::NotFound&&result.message,"missing route retains input without dispatch");}
    {Harness h;h.route->selectable=false;std::error_code ec;auto result=h.send(MessageForTest(),ec);Check(ec==ErrorCode::NotFound&&result.message,"unselectable route retains input without dispatch");}
    {
      std::vector<std::future<bool>> tasks;
      for(int worker=0;worker<8;++worker)tasks.push_back(std::async(std::launch::async,[]{
        for(int n=0;n<64;++n){Harness h;h.timeout_ms=n%2?0:20;std::thread completion;
          h.dispatch=[&](auto call){completion=std::thread([call]{call->Complete();});};std::error_code ec;
          auto result=h.send(MessageForTest(),ec);const auto returned=ec;completion.join();
          if(ec!=returned||(!ec&&(result.message_id!="owned-mid"||!result.message))||(ec&&ec!=ErrorCode::RequestTimeout))return false;
        }return true;
      }));
      for(auto& task:tasks)Check(task.get(),"completion-deadline race selects one stable outcome");
    }
    std::cout<<"SDK_SYNC_SEND_WAIT_OWNERSHIP_CONTRACTS_PASS actual_RPC_and_performance_pending=1\n";return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
