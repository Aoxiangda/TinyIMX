#include "services/message/server/MessageRpcPollerPolicy.h"
#include "services/message/server/MessageServiceServer.h"
#include "tinyimx/message/v1/message_service.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <chrono>
using namespace tinyimx::message;
int checks=0,failures=0;
void Check(bool v,const char* s){++checks;if(!v)++failures;std::cout<<(v?"[PASS] ":"[FAIL] ")<<s<<'\n';}
class Fake final:public tinyimx::message::v1::MessageService::Service {
public:
 grpc::Status GetGroupMessageDelivery(grpc::ServerContext*,const tinyimx::message::v1::GetGroupMessageDeliveryRequest* in,tinyimx::message::v1::GetGroupMessageDeliveryResponse* out) override {
  if(!in->message_id()||!in->recipient_user_id())return {grpc::StatusCode::INVALID_ARGUMENT,"owned nativeinvalid arguments"};
  auto* work=out->mutable_work();work->mutable_message()->set_message_id(in->message_id());work->mutable_message()->set_content("native-exact-"+std::to_string(in->message_id()));work->mutable_delivery()->set_message_id(in->message_id());work->mutable_delivery()->set_recipient_user_id(in->recipient_user_id());return grpc::Status::OK;
 }
};
int main(int argc,char** argv){
 if(argc!=2)return 2;const bool expected=std::string(argv[1])=="1";
 for(const char* value:{static_cast<const char*>(nullptr),"","0","01","true","2","-1"}){auto p=ParseMessageRpcPollerPolicy(value);Check(!p.enabled&&p.queues==1&&p.min_pollers==1&&p.max_pollers==2,"absent/invalid preserve defaultprofile");}
 auto on=ParseMessageRpcPollerPolicy("1");Check(on.enabled&&on.queues==1&&on.min_pollers==4&&on.max_pollers==8,"explicit one boundedprofile");
 auto actual=ConfiguredMessageRpcPollerPolicy();Check(actual.enabled==expected,"real ENV actualpolicy exact");
 MessageServiceServer missing(nullptr);Check(!missing.Start("127.0.0.1:0")&&!missing.Running(),"nullservice cannotlisten");
 Fake fake;MessageServiceServer server(&fake);Check(!server.SetReady(true),"readiness beforestart rejected");Check(!server.Start(""),"emptytarget rejected");
 Check(server.Start("127.0.0.1:0"),"own ephemeral loopbackstarts");Check(server.Running()&&server.SelectedPort()>0&&server.BoundTarget()=="127.0.0.1:"+std::to_string(server.SelectedPort()),"bound identity exact");Check(!server.Start("127.0.0.1:0"),"duplicatestart rejected");Check(server.SetReady(false)&&server.SetReady(true),"readiness changes accepted");
 auto channel=grpc::CreateChannel(server.BoundTarget(),grpc::InsecureChannelCredentials());Check(channel->WaitForConnected(std::chrono::system_clock::now()+std::chrono::seconds(3)),"own channelconnects");auto stub=tinyimx::message::v1::MessageService::NewStub(channel);
 std::atomic<int> completed{0},bad{0};std::vector<std::thread> workers;
 for(int i=0;i<16;++i)workers.emplace_back([&,i]{for(int j=0;j<4;++j){tinyimx::message::v1::GetGroupMessageDeliveryRequest in;auto id=static_cast<std::uint64_t>(i*4+j+1);in.set_message_id(id);in.set_recipient_user_id(id+100);tinyimx::message::v1::GetGroupMessageDeliveryResponse out;grpc::ClientContext ctx;ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));auto status=stub->GetGroupMessageDelivery(&ctx,in,&out);if(!status.ok()||out.work().message().message_id()!=id||out.work().message().content()!="native-exact-"+std::to_string(id)||out.work().delivery().message_id()!=id||out.work().delivery().recipient_user_id()!=id+100)++bad;++completed;}});
 for(auto& w:workers)w.join();Check(completed==64&&bad==0,"64 concurrent RPC identities preserved");
 {tinyimx::message::v1::GetGroupMessageDeliveryRequest in;tinyimx::message::v1::GetGroupMessageDeliveryResponse out;grpc::ClientContext ctx;ctx.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));Check(stub->GetGroupMessageDelivery(&ctx,in,&out).error_code()==grpc::StatusCode::INVALID_ARGUMENT,"invalid request status preserved");}
 std::thread waiter([&]{server.Wait();});server.Shutdown();waiter.join();Check(!server.Running()&&!server.SetReady(true),"shutdown andreadiness fence correct");server.Shutdown();Check(!server.Start("127.0.0.1:0"),"no unsafe restart aftershutdown");
 std::cout<<"{\"status\":\"MESSAGE_RPC_POLLERS_NATIVE_"<<(failures?"FAIL":"PASS")<<"\",\"checks\":"<<checks<<",\"failures\":"<<failures<<",\"completed_rpc\":"<<completed<<",\"enabled\":"<<(actual.enabled?1:0)<<",\"min_pollers\":"<<actual.min_pollers<<",\"max_pollers\":"<<actual.max_pollers<<"}"<<'\n';return failures?1:0;
}
