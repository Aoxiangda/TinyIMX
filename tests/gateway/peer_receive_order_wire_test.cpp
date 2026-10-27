// PERF076: actual GatewayServer dispatch -> real gRPC -> actual TCP delivery.
// Only the two dependency services and peer lease snapshot are test fixtures.
// This is not a MySQL durability, discovery, or performance benchmark.
#include "gateway/GatewayServer.h"
#include "gateway/business/BusinessExecutor.h"
#include "common/net/EventLoop.h"
#include "common/protocol/GatewayPeerProtocol.h"
#include "common/protocol/ClientChatProtocol.h"
#include "services/rpc/MessageRpcClient.h"
#include "services/rpc/UserRpcClient.h"
#include "services/rpc/StaticServiceEndpointProvider.h"
#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <condition_variable>
#include <deque>
#include <future>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>

namespace {
using namespace tinyimx;
using namespace std::chrono_literals;
using Json = nlohmann::json;
namespace mp = tinyimx::message::v1;
namespace up = tinyimx::user::v1;
void Require(bool ok, const char* text) {
    if (!ok) throw std::runtime_error(text);
    std::cout << "PASS " << text << std::endl;
}
std::string Body(std::uint64_t mid) {
    return Json{{"from",500001},{"to",500002},{"text","order-"+std::to_string(mid)}}.dump();
}
class Messages final : public mp::MessageService::Service {
public:
    std::mutex mutex; std::condition_variable cv;
    bool first_entered=false, second_entered=false, release=false;
    grpc::Status GetPrivateMessage(grpc::ServerContext*, const mp::GetPrivateMessageRequest* q,
                                   mp::GetPrivateMessageResponse* r) override {
        {
            std::unique_lock lock(mutex);
            if (q->message_id()==1) {
                first_entered=true; cv.notify_all();
                if (!cv.wait_for(lock,3s,[&]{return release;}))
                    return {grpc::StatusCode::DEADLINE_EXCEEDED,"fixture first-Get watchdog"};
            } else {second_entered=true; cv.notify_all();}
        }
        auto* row=r->mutable_record();
        row->set_message_id(q->message_id()); row->set_from_user_id(500001);
        row->set_to_user_id(500002); row->set_message_type(1);
        row->set_content(Body(q->message_id())); row->set_delivery_state(mp::MESSAGE_DELIVERY_STATE_PENDING);
        return grpc::Status::OK;
    }
    void Release() {std::lock_guard lock(mutex); release=true; cv.notify_all();}
};
class Users final : public up::UserService::Service {
    grpc::Status Authenticate(grpc::ServerContext*, const up::AuthenticateRequest* q,
                              up::AuthenticateResponse* r) override {
        if(q->username()!="order-receiver"||q->password()!="fixture-only")
            return {grpc::StatusCode::UNAUTHENTICATED,"fixture credentials"};
        r->set_result(up::AUTHENTICATE_RESULT_AUTHENTICATED);
        auto* profile=r->mutable_profile(); profile->set_user_id(500002);
        profile->set_username(q->username()); profile->set_user_status(1);
        return grpc::Status::OK;
    }
};
struct Wire {
    int fd=-1; Buffer input; ProtocolCodec codec; std::deque<Packet> ready;
    explicit Wire(std::uint16_t port) {
        fd=::socket(AF_INET,SOCK_STREAM,0); Require(fd>=0,"owned TCP socket");
        sockaddr_in address{}; address.sin_family=AF_INET; address.sin_port=htons(port);
        address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        Require(::connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"loopback Gateway connected");
    }
    ~Wire(){if(fd>=0)::close(fd);}
    void Send(const Packet& packet) {
        Buffer bytes; Require(codec.Encode(packet,&bytes),"real protocol encode");
        std::size_t n=0;
        while(n<bytes.ReadableBytes()) {
            const auto count=::send(fd,bytes.Peek()+n,bytes.ReadableBytes()-n,MSG_NOSIGNAL);
            if(count<=0)throw std::runtime_error("wire send failed");
            n+=count;
        }
    }
    std::optional<Packet> Read(std::chrono::milliseconds timeout) {
        const auto end=std::chrono::steady_clock::now()+timeout;
        for(;;) {
            if(!ready.empty()){auto p=std::move(ready.front());ready.pop_front();return p;}
            const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(end-std::chrono::steady_clock::now());
            if(left<=0ms)return std::nullopt;
            pollfd pfd{fd,POLLIN,0}; if(::poll(&pfd,1,static_cast<int>(left.count()))<=0)return std::nullopt;
            int error=0; if(input.ReadFd(fd,&error)<=0)throw std::runtime_error("wire read failed");
            auto decoded=codec.Decode(&input);
            if(decoded.status!=DecodeStatus::kOk&&decoded.status!=DecodeStatus::kNeedMoreData)
                throw std::runtime_error(decoded.error_message);
            for(auto& packet:decoded.packets)ready.push_back(std::move(packet));
        }
    }
};
std::uint16_t FreePort() {
    int fd=::socket(AF_INET,SOCK_STREAM,0); Require(fd>=0,"port reservation socket");
    sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    Require(::bind(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))==0,"temporary port reservation");
    socklen_t size=sizeof(a);Require(::getsockname(fd,reinterpret_cast<sockaddr*>(&a),&size)==0,"owned port read");
    const auto port=ntohs(a.sin_port);::close(fd);return port;
}
Packet Forward(std::uint64_t mid,std::string lease="fixture-lease",bool tamper=false) {
    GatewayForwardChatRequest q; q.message_id=mid; q.from_user_id=500001;q.to_user_id=500002;
    q.source_gateway_id="fixture-peer";q.source_lease_token=lease;
    q.message_body=tamper?"tampered":Body(mid);
    Packet p;p.type=MessageType::kGatewayForwardChatRequest;p.seq=100+mid;
    Require(SerializeGatewayForwardChatRequest(q,&p.body),"peer request serialization");return p;
}
}

int main(int argc,char** argv) {
    try {
        Require(argc==2,"explicit expected ordering mode");
        const bool expect_reorder=std::string(argv[1])=="--expect-reorder";
        Require(expect_reorder||std::string(argv[1])=="--expect-fifo","allowed expected mode");
        Messages messages;Users users;grpc::ServerBuilder builder;int rpc_port=0;
        builder.AddListeningPort("127.0.0.1:0",grpc::InsecureServerCredentials(),&rpc_port);
        builder.RegisterService(&messages);builder.RegisterService(&users);
        auto server=builder.BuildAndStart();Require(server&&rpc_port,"owned fixture gRPC services");
        const auto endpoint="127.0.0.1:"+std::to_string(rpc_port);
        auto provider=std::make_shared<rpc::StaticServiceEndpointProvider>("",endpoint,endpoint);
        rpc::MessageRpcClient message_client(provider);rpc::UserRpcClient user_client(provider);
        EventLoop loop;BusinessExecutorOptions options;options.worker_threads=2;options.max_pending_tasks=16;
        const auto* exact_key=std::getenv("TINYIMX_MESSAGE_EXACT_KEY_ENABLE");
        options.isolate_ordering_keys=exact_key && std::string(exact_key)=="1";
        options.per_stripe_queue_capacity=8;BusinessExecutor executor(options);
        Require(executor.Start(),"production BusinessExecutor started");
        GatewayServerOptions go;go.gateway_id="fixture-receiver";go.receiver_ack_timeout=5s;
        const auto port=FreePort();GatewayServer gateway(&loop,InetAddress("127.0.0.1",port),go);
        gateway.SetBusinessExecutor(&executor);gateway.SetMessageExecutor(&executor);
        gateway.SetUserRpcClient(&user_client);
        gateway.SetGatewayPeerVerifyCallback([](const auto& id,const auto& token,std::string*){
            return id=="fixture-peer"&&token=="fixture-lease";
        });
        Require(gateway.Start(),"actual production GatewayServer started");
        std::exception_ptr failure;Json result;
        std::thread client([&] {
            try {
                Wire receiver(port),peer(port);
                Packet login;login.type=MessageType::kLoginRequest;login.seq=1;
                login.body=Json{{"username","order-receiver"},{"password","fixture-only"}}.dump();receiver.Send(login);
                auto accepted=receiver.Read(2s);Require(accepted&&accepted->type==MessageType::kLoginResponse&&
                    Json::parse(accepted->body).value("success",false),"actual Gateway login creates receiver session");
                std::promise<void> installed;auto wait=installed.get_future();
                loop.QueueInLoop([&]{gateway.SetMessageRpcClient(&message_client);installed.set_value();});wait.get();
                peer.Send(Forward(7,"wrong-lease"));auto denied=peer.Read(2s);GatewayForwardChatResponse response;
                Require(denied&&DeserializeGatewayForwardChatResponse(denied->body,&response)&&
                    response.status==GatewayForwardChatStatus::kUnauthorized,"invalid peer lease rejected on real wire");
                peer.Send(Forward(8,"fixture-lease",true));auto bad=peer.Read(2s);
                Require(bad&&DeserializeGatewayForwardChatResponse(bad->body,&response)&&
                    response.status==GatewayForwardChatStatus::kInvalidRequest,"durable identity/content mismatch rejected on real wire");
                peer.Send(Forward(1));
                {std::unique_lock lock(messages.mutex);Require(messages.cv.wait_for(lock,2s,[&]{return messages.first_entered;}),"first production dispatch reaches gated Get");}
                peer.Send(Forward(2));std::vector<std::uint64_t> deliveries;
                auto early=receiver.Read(300ms);
                if(early){ServerChatDelivery d;Require(early->type==MessageType::kChatDelivery&&
                    DeserializeServerChatDelivery(early->body,&d),"early delivery real body decoded");
                    Require(d.from_user_id==500001&&d.to_user_id==500002&&d.text=="order-"+std::to_string(d.message_id),"early delivery identity and content exact");deliveries.push_back(d.message_id);}
                messages.Release();
                while(deliveries.size()<2){auto packet=receiver.Read(2s);ServerChatDelivery d;
                    Require(packet&&packet->type==MessageType::kChatDelivery&&DeserializeServerChatDelivery(packet->body,&d),"actual receiver delivery decoded");
                    Require(d.from_user_id==500001&&d.to_user_id==500002&&d.text=="order-"+std::to_string(d.message_id),"actual delivery identity and content exact");deliveries.push_back(d.message_id);}
                std::vector<std::uint64_t> responses;
                while(responses.size()<2){auto packet=peer.Read(2s);Require(packet&&DeserializeGatewayForwardChatResponse(packet->body,&response)&&response.Delivered(),"actual peer completion delivered");responses.push_back(response.message_id);}
                peer.Send(Forward(1));auto duplicate=peer.Read(2s);
                Require(duplicate&&DeserializeGatewayForwardChatResponse(duplicate->body,&response)&&response.Delivered()&&response.duplicate,"duplicate MID suppressed on real wire");
                Require(!receiver.Read(100ms),"duplicate MID causes no additional receiver push");
                const auto expected=expect_reorder?std::vector<std::uint64_t>{2,1}:std::vector<std::uint64_t>{1,2};
                result={{"actual_receiver_order",deliveries},{"actual_peer_response_order",responses},
                    {"expected_reorder",expect_reorder},{"async_env",std::getenv("TINYIMX_GATEWAY_ASYNC_MESSAGE_RPC_ENABLE")?std::getenv("TINYIMX_GATEWAY_ASYNC_MESSAGE_RPC_ENABLE"):"0"},
                    {"scope","real Gateway dispatch/gRPC/TCP delivery; fixture User/Message services and exact lease snapshot; no SQL durability claim"}};
                Require(deliveries==expected,"production receiver order matches explicit expected contract");
            } catch(...) {failure=std::current_exception();}
            messages.Release();
            loop.QueueInLoop([&]{gateway.StopPrivateReplayAdmission();});
            try {executor.BeginDrain();Require(executor.ShutdownGraceful(),"accepted production tasks drained before stop");}
            catch(...) {if(!failure)failure=std::current_exception();}
            loop.QueueInLoop([&]{gateway.Stop();loop.Quit();});
        });
        loop.Loop();client.join();server->Shutdown();server->Wait();
        if(failure)std::rethrow_exception(failure);
        Require(executor.GetStats().current_pending_tasks==0,"production logical pending zero");
        std::cout<<"WIRE_ORDER_RESULT "<<result.dump()<<std::endl;return 0;
    } catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
}
