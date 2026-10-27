#include "tinyimx/message/v1/message_service.grpc.pb.h"
#include "tinyimx/file/v1/file_service.grpc.pb.h"
#include <google/protobuf/util/json_util.h>
#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
namespace {
using Json=nlohmann::json;
template<class Request,class Response,class F>
Json Invoke(const Json& body,F&& call) {
    Request request;
    const auto parsed=google::protobuf::util::JsonStringToMessage(body.dump(),&request);
    if(!parsed.ok())throw std::runtime_error("Owned request JSON does not match pinned RPC descriptor");
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(4));
    Response response;
    const auto status=call(&context,request,&response);
    std::string encoded;
    if(!google::protobuf::util::MessageToJsonString(response,&encoded).ok())
        throw std::runtime_error("Pinned RPC response JSON encoding failed");
    return Json{{"status_code",static_cast<int>(status.error_code())},
                {"status_message",status.error_message()},{"response",Json::parse(encoded)}};
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=4)return 2;
        const std::string service=argv[1],target=argv[2],method=argv[3];
        const std::string port=service=="message"?"50053":service=="file"?"50055":"";
        if(port.empty()||!std::regex_match(target,std::regex("172\\.18\\.[0-9]{1,3}\\.[0-9]{1,3}:"+port)))
            throw std::runtime_error("Only owned lab Docker Message/File endpoints are allowed");
        std::string input,line;
        while(std::getline(std::cin,line)) {
            input+=line;
            if(input.size()>8*1024*1024)throw std::runtime_error("Finite owned RPC request exceeds limit");
        }
        auto body=Json::parse(input);
        const auto actor=body.at("actor_user_id").is_string()
            ?std::stoull(body.at("actor_user_id").get<std::string>())
            :body.at("actor_user_id").get<std::uint64_t>();
        if(actor<500001||actor>500008)throw std::runtime_error("Only the eight owned fixture identities are allowed");
        body["meta"]=Json{{"request_id","owned-full-business-r884"},{"caller_service","owned-business-qualification"}};
        auto channel=grpc::CreateChannel(target,grpc::InsecureChannelCredentials());
        if(!channel->WaitForConnected(std::chrono::system_clock::now()+std::chrono::seconds(4)))
            throw std::runtime_error("Owned RPC endpoint not ready");
        Json result;
        if(service=="message") {
            if(method!="ListGroupHistory")throw std::runtime_error("Read-only group-history method required");
            auto stub=tinyimx::message::v1::MessageService::NewStub(channel);
            result=Invoke<tinyimx::message::v1::ListGroupHistoryRequest,tinyimx::message::v1::ListGroupHistoryResponse>(
                body,[&](auto*c,const auto&q,auto*r){return stub->ListGroupHistory(c,q,r);});
        } else {
            auto stub=tinyimx::file::v1::FileService::NewStub(channel);
#define OWNED_FILE_CALL(Name) if(method==#Name)result=Invoke<tinyimx::file::v1::Name##Request,tinyimx::file::v1::Name##Response>(body,[&](auto*c,const auto&q,auto*r){return stub->Name(c,q,r);});
            OWNED_FILE_CALL(BeginUpload)
            else OWNED_FILE_CALL(GetUploadSession)
            else OWNED_FILE_CALL(CancelUpload)
            else OWNED_FILE_CALL(UploadChunk)
            else OWNED_FILE_CALL(GetUploadProgress)
            else OWNED_FILE_CALL(FinalizeUpload)
            else OWNED_FILE_CALL(GetDownloadInfo)
            else OWNED_FILE_CALL(ReadFileRange)
            else throw std::runtime_error("Pinned owned File method required");
#undef OWNED_FILE_CALL
        }
        std::cout<<result.dump()<<'\n';
        return 0;
    } catch(const std::exception&e) {
        std::cerr<<"Owned business probe failed: "<<e.what()<<'\n';return 1;
    }
}
