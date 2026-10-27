// Finite ACK-then-process-kill recovery probe, not a throughput benchmark.
#include "tinyimx/message/v1/message_service.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>

namespace v1=tinyimx::message::v1;
using namespace std::chrono_literals;
constexpr unsigned count=128,workers=8;
std::string Body(unsigned i) {auto s="owned-crash-r442-body:"+std::to_string(i)+":";s.resize(128,'x');return s;}
int main(int argc,char**argv)try{
    if(argc!=4)return 2;const std::string target=argv[1],mode=argv[2];
    if(mode!="create"&&mode!="verify")return 2;
    const auto marker=std::filesystem::path(argv[3]);
    if(mode=="create"&&(!marker.is_absolute()||marker.parent_path()!=std::filesystem::path("/home/jackson7/projects/tinyimx-interview-20261017/perf442-native-crash-recovery-r1")||marker.filename()!="crash-issued"||std::filesystem::exists(marker)))return 2;
    auto channel=grpc::CreateChannel(target,grpc::InsecureChannelCredentials());
    if(!channel->WaitForConnected(std::chrono::system_clock::now()+5s))throw std::runtime_error("independent Message connection failed");
    std::vector<std::unique_ptr<v1::MessageService::Stub>> stubs;
    for(unsigned i=0;i<workers;++i)stubs.push_back(v1::MessageService::NewStub(channel));
    std::array<std::uint64_t,count> mids{};std::array<std::string,count> errors;
    std::atomic<unsigned> next{0},completed{0};std::vector<std::jthread> threads;
    for(unsigned w=0;w<workers;++w)threads.emplace_back([&,w]{
        for(;;){const auto i=next.fetch_add(1);if(i>=count)return;
            v1::PersistPrivateMessageRequest q;q.set_from_user_id(500003);q.set_to_user_id(500002);q.set_client_message_id("native-crash-r442-"+std::to_string(i));q.set_message_type(1);q.set_content(Body(i));
            v1::PersistPrivateMessageResponse r;grpc::ClientContext context;context.set_deadline(std::chrono::system_clock::now()+5s);
            const auto status=stubs[w]->PersistPrivateMessage(&context,q,&r);
            const auto expected=mode=="create"?v1::PERSIST_PRIVATE_MESSAGE_RESULT_CREATED:v1::PERSIST_PRIVATE_MESSAGE_RESULT_REUSED;
            if(!status.ok()||r.result()!=expected||!r.message_id()||r.record().content()!=Body(i)){
                errors[i]="status="+std::to_string(status.error_code())+",result="+std::to_string(r.result());continue;
            }
            mids[i]=r.message_id();++completed;
        }
    });
    threads.clear();std::set<std::uint64_t> unique(mids.begin(),mids.end());
    const bool passed=completed==count&&unique.size()==count&&!unique.contains(0);
    nlohmann::json result={{"mode",mode},{"messages",count},{"completed",completed.load()},{"unique_mids",unique.size()},{"mids",mids},{"errors",errors},{"passed",passed},{"payload_bytes",128},{"from",500003},{"to",500002},{"functional_only",true}};
    std::cout<<"CRASH442_ACKS "<<result.dump()<<std::endl;
    // Keep channel/stubs alive until the parent has issued the exact-PID fault.
    if(mode=="create"){
        const auto deadline=std::chrono::steady_clock::now()+15s;
        while(!std::filesystem::exists(marker)){
            if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("parent fault handshake timed out");
            std::this_thread::sleep_for(1ms);
        }
    }
    return passed?0:1;
}catch(const std::exception&e){std::cerr<<"CRASH442_FAIL "<<e.what()<<std::endl;return 1;}
