// Dedicated desktop ingress. File bytes never enter the chat Gateway/executor.
// Authenticated identity is derived from UserService, never from JSON actor IDs.
#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "common/net/EventLoop.h"
#include "common/net/TcpServer.h"
#include "common/concurrency/ThreadPool.h"
#include "services/intelligence/mcp/HttpCodec.h"
#include "services/rpc/StaticServiceEndpointProvider.h"
#include "services/rpc/UserRpcClient.h"
#include "services/rpc/GroupRpcClient.h"
#include "tinyimx/file/v1/file_service.grpc.pb.h"
#include "tinyimx/message/v1/message_service.grpc.pb.h"
#include <google/protobuf/util/json_util.h>
#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>
#include <openssl/rand.h>
#include <openssl/hmac.h>
#include <openssl/crypto.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <csignal>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <stdexcept>
#include <iostream>
using Json=nlohmann::json;
namespace {
tinyimx::EventLoop* signal_loop=nullptr;
void stop(int){if(signal_loop)signal_loop->Quit();}
std::int64_t now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::string hex(const unsigned char* p,std::size_t n){static const char* digits="0123456789abcdef";std::string out;out.reserve(n*2);for(std::size_t i=0;i<n;++i){out+=digits[p[i]>>4];out+=digits[p[i]&15];}return out;}
std::string unhex(const std::string &s){if(s.size()%2)throw std::runtime_error("invalid hex");std::string out;for(std::size_t i=0;i<s.size();i+=2){auto nibble=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::runtime_error("invalid hex");};out+=char(nibble(s[i])*16+nibble(s[i+1]));}return out;}
std::string randomToken(){unsigned char bytes[32];if(RAND_bytes(bytes,sizeof(bytes))!=1)throw std::runtime_error("randomness unavailable");return hex(bytes,sizeof(bytes));}
std::string signingKey(const char* path){
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd>=0){unsigned char bytes[32];if(RAND_bytes(bytes,sizeof(bytes))!=1||write(fd,bytes,sizeof(bytes))!=sizeof(bytes)||fsync(fd)!=0){close(fd);throw std::runtime_error("cannot create signing key");}close(fd);}
    fd=open(path,O_RDONLY|O_NOFOLLOW);struct stat st{};if(fd<0||fstat(fd,&st)!=0||!S_ISREG(st.st_mode)||st.st_size!=32||(st.st_mode&077)!=0){if(fd>=0)close(fd);throw std::runtime_error("signing key must be a private 32-byte regular file");}
    std::string out(32,'\0');if(read(fd,out.data(),out.size())!=32){close(fd);throw std::runtime_error("cannot read signing key");}close(fd);return out;
}
std::uint64_t number(const Json &b,const char* key){const auto &v=b.at(key);std::uint64_t n=0;if(v.is_string()){const auto s=v.get<std::string>();if(s.empty()||s.size()>20||s.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("invalid ID");n=std::stoull(s);}else if(v.is_number_unsigned())n=v.get<std::uint64_t>();else if(v.is_number_integer()&&v.get<std::int64_t>()>0)n=static_cast<std::uint64_t>(v.get<std::int64_t>());if(!n)throw std::runtime_error("invalid ID");return n;}
Json jsonProto(const google::protobuf::Message &message){std::string out;google::protobuf::util::JsonPrintOptions options;options.preserve_proto_field_names=true;auto status=google::protobuf::util::MessageToJsonString(message,&out,options);if(!status.ok())throw std::runtime_error("RPC JSON conversion failed");auto b=Json::parse(out);if(b.contains("file")){b["file"].erase("storage_key");b["file"].erase("storage_backend");}return b;}
struct Failure{int status;std::string message;};
class Ingress {
    struct Session{std::uint64_t actor;std::int64_t expires;};
    struct Rate{std::int64_t minute;int count;};
    std::mutex mutex_;std::unordered_map<std::string,Session> sessions_;std::unordered_map<std::string,Rate> rates_;
    std::string secret_;tinyimx::rpc::UserRpcClient users_;tinyimx::rpc::GroupRpcClient groups_;
    std::unique_ptr<tinyimx::file::v1::FileService::Stub> files_;
    std::unique_ptr<tinyimx::message::v1::MessageService::Stub> messages_;
    std::string signature(const std::string &data){unsigned char digest[EVP_MAX_MD_SIZE];unsigned int size=0;HMAC(EVP_sha256(),secret_.data(),secret_.size(),reinterpret_cast<const unsigned char*>(data.data()),data.size(),digest,&size);return hex(digest,size);}
    std::string ticket(const Json &claims){const auto payload=claims.dump();return hex(reinterpret_cast<const unsigned char*>(payload.data()),payload.size())+"."+signature(payload);}
    Json verify(const std::string &value){if(value.size()>4096)throw Failure{403,"invalid share capability"};const auto p=value.find('.');if(p==std::string::npos)throw Failure{403,"invalid share capability"};const auto payload=unhex(value.substr(0,p));const auto expected=signature(payload),actual=value.substr(p+1);if(expected.size()!=actual.size()||CRYPTO_memcmp(expected.data(),actual.data(),actual.size())!=0)throw Failure{403,"invalid share capability"};auto claims=Json::parse(payload);if(claims.at("expires").get<std::int64_t>()<now())throw Failure{403,"share capability expired"};return claims;}
    static tinyimx::rpc::RpcCallOptions options(){tinyimx::rpc::RpcCallOptions o;o.caller_service="desktop-file-ingress";o.remaining_timeout=std::chrono::milliseconds(5000);return o;}
    template<class Request,class Response,class Function> Json call(Json args,std::uint64_t actor,Function function){
        if(!args.is_object()||args.contains("actor_user_id")||args.contains("meta"))throw Failure{400,"identity and metadata are server-controlled"};
        Request request;Response response;auto parsed=google::protobuf::util::JsonStringToMessage(args.dump(),&request);if(!parsed.ok())throw Failure{400,"invalid operation arguments"};request.set_actor_user_id(actor);
        grpc::ClientContext context;context.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(5));
        auto status=function(&context,request,&response);if(!status.ok()){const auto code=status.error_code();throw Failure{code==grpc::StatusCode::PERMISSION_DENIED?403:code==grpc::StatusCode::NOT_FOUND?404:code==grpc::StatusCode::INVALID_ARGUMENT?400:code==grpc::StatusCode::FAILED_PRECONDITION?409:503,status.error_message()};}
        return jsonProto(response);
    }
    Json info(std::uint64_t actor,std::uint64_t file){return call<tinyimx::file::v1::GetDownloadInfoRequest,tinyimx::file::v1::GetDownloadInfoResponse>({{"file_id",std::to_string(file)}},actor,[this](auto c,const auto&r,auto*s){return files_->GetDownloadInfo(c,r,s);});}
public:
    Ingress(std::shared_ptr<const tinyimx::rpc::ServiceEndpointProvider> endpoints,std::string key,const std::string &file,const std::string &message):secret_(std::move(key)),users_(endpoints),groups_(endpoints),files_(tinyimx::file::v1::FileService::NewStub(grpc::CreateChannel(file,grpc::InsecureChannelCredentials()))),messages_(tinyimx::message::v1::MessageService::NewStub(grpc::CreateChannel(message,grpc::InsecureChannelCredentials()))){}
    Json handle(const Json &body,const std::string &authorization,const std::string &peer){
        const auto op=body.at("op").get<std::string>();auto args=body.value("args",Json::object());
        if(op=="login"){
            {std::lock_guard lock(mutex_);const auto minute=now()/60;for(auto it=rates_.begin();it!=rates_.end();)if(it->second.minute<minute)it=rates_.erase(it);else ++it;if(rates_.size()>1024)throw Failure{429,"login rate table full"};auto &rate=rates_[peer];if(rate.minute!=minute)rate={minute,0};if(++rate.count>30)throw Failure{429,"login rate limited"};}
            const auto username=args.at("username").get<std::string>(),password=args.at("password").get<std::string>();if(username.empty()||username.size()>128||password.empty()||password.size()>256)throw Failure{400,"invalid credentials"};
            const auto result=users_.Authenticate({username,password},options());if(!result.ok()||!result.value->authenticated())throw Failure{401,"authentication failed"};
            const auto token=randomToken();std::lock_guard lock(mutex_);for(auto it=sessions_.begin();it!=sessions_.end();)if(it->second.expires<now())it=sessions_.erase(it);else ++it;if(sessions_.size()>=1024)throw Failure{503,"session capacity reached"};
            sessions_[token]={result.value->profile->user_id,now()+8*3600};return {{"token",token},{"user_id",std::to_string(result.value->profile->user_id)},{"expires_in",8*3600}};
        }
        if(authorization.rfind("Bearer ",0)!=0)throw Failure{401,"authentication required"};const auto token=authorization.substr(7);std::uint64_t actor;
        {std::lock_guard lock(mutex_);auto it=sessions_.find(token);if(it==sessions_.end()||it->second.expires<now())throw Failure{401,"session expired"};actor=it->second.actor;if(op=="logout"){sessions_.erase(it);return {{"revoked",true}};}}
        if(args.contains("actor_user_id")||args.contains("meta"))throw Failure{400,"identity and metadata are server-controlled"};
        using namespace tinyimx::file::v1;
        if(op=="share"){
            const auto file=number(args,"file_id");const auto metadata=info(actor,file).at("info");Json claims{{"owner",std::to_string(actor)},{"file_id",std::to_string(file)},{"sha256",metadata.at("verified_checksum")},{"expires",now()+7*24*3600}};
            if(args.contains("group_id")){const auto group=number(args,"group_id");if(!groups_.GetGroup({actor,group},options()).ok())throw Failure{403,"group membership denied"};claims["group_id"]=std::to_string(group);}
            else{const auto target=number(args,"target_user_id");if(!users_.GetUserProfile({target},options()).ok())throw Failure{404,"recipient not found"};claims["recipient"]=std::to_string(target);}
            return {{"capability",ticket(claims)},{"info",metadata}};
        }
        if(op=="download_info"||op=="read_range"){
            std::uint64_t owner=actor;Json claims;const auto capability=args.value("capability",std::string{});args.erase("capability");const auto file=number(args,"file_id");
            if(!capability.empty()){
                claims=verify(capability);if(number(claims,"file_id")!=file)throw Failure{403,"capability file mismatch"};
                if(claims.contains("group_id")){if(!groups_.GetGroup({actor,number(claims,"group_id")},options()).ok())throw Failure{403,"group membership denied"};}
                else if(number(claims,"recipient")!=actor&&number(claims,"owner")!=actor)throw Failure{403,"capability recipient mismatch"};
                owner=number(claims,"owner");const auto metadata=info(owner,file);if(metadata.at("info").at("verified_checksum")!=claims.at("sha256"))throw Failure{409,"immutable file validator changed"};
                if(op=="download_info")return metadata;
                args["if_match_sha256"]=claims.at("sha256");
            }
            if(op=="download_info")return info(owner,file);
            const auto length=number(args,"length");if(length>65536)throw Failure{400,"range exceeds 64 KiB"};
            return call<ReadFileRangeRequest,ReadFileRangeResponse>(args,owner,[this](auto c,const auto&r,auto*s){return files_->ReadFileRange(c,r,s);});
        }
        if(op=="group_history")return call<tinyimx::message::v1::ListGroupHistoryRequest,tinyimx::message::v1::ListGroupHistoryResponse>(args,actor,[this](auto c,const auto&r,auto*s){return messages_->ListGroupHistory(c,r,s);});
        if(op=="begin")return call<BeginUploadRequest,BeginUploadResponse>(args,actor,[this](auto c,const auto&r,auto*s){return files_->BeginUpload(c,r,s);});
        if(op=="progress")return call<GetUploadProgressRequest,GetUploadProgressResponse>(args,actor,[this](auto c,const auto&r,auto*s){return files_->GetUploadProgress(c,r,s);});
        if(op=="cancel")return call<CancelUploadRequest,CancelUploadResponse>(args,actor,[this](auto c,const auto&r,auto*s){return files_->CancelUpload(c,r,s);});
        if(op=="finalize")return call<FinalizeUploadRequest,FinalizeUploadResponse>(args,actor,[this](auto c,const auto&r,auto*s){return files_->FinalizeUpload(c,r,s);});
        if(op=="chunk"){
            if(args.at("data").get<std::string>().size()>350000)throw Failure{413,"chunk exceeds 256 KiB"};
            return call<UploadChunkRequest,UploadChunkResponse>(args,actor,[this](auto c,const auto&r,auto*s){return files_->UploadChunk(c,r,s);});
        }
        throw Failure{404,"unknown operation"};
    }
};
}
int main(int argc,char**argv){
    if(argc!=8){std::cerr<<"usage: desktop_file_server CONFIG KEY USER_RPC FILE_RPC MESSAGE_RPC GROUP_RPC PORT\n";return 2;}
    try{
        tinyimx::Config config;if(!config.LoadFromFile(argv[1])||!tinyimx::Logger::Instance().Init(config.Logger()))return 2;
        auto endpoints=std::make_shared<tinyimx::rpc::StaticServiceEndpointProvider>("",argv[3],argv[5],argv[6],argv[4]);Ingress ingress(endpoints,signingKey(argv[2]),argv[4],argv[5]);
        tinyimx::ThreadPoolOptions poolOptions;poolOptions.name="desktop-file";poolOptions.worker_threads=4;poolOptions.queue_capacity=64;poolOptions.enable_dynamic_resize=false;poolOptions.min_threads=4;poolOptions.max_threads=4;poolOptions.queue_full_policy=tinyimx::QueueFullPolicy::kDiscard;
        tinyimx::ThreadPool workers(poolOptions);if(!workers.Start())return 2;
        tinyimx::EventLoop loop;signal_loop=&loop;std::signal(SIGINT,stop);std::signal(SIGTERM,stop);
        const auto port=std::stoi(argv[7]);if(port<1||port>65535)return 2;
        tinyimx::TcpServer server(&loop,tinyimx::InetAddress("0.0.0.0",static_cast<std::uint16_t>(port)),"desktop-file",1);
        server.SetIdleTimeout(std::chrono::seconds(15),std::chrono::seconds(5));tinyimx::mcp::HttpCodec codec(512*1024);
        auto reply=[](const tinyimx::TcpConnectionPtr &c,int status,Json b){if(c&&c->IsConnected()){c->Send(tinyimx::mcp::HttpCodec::EncodeJsonResponse(status,b.dump()));c->Shutdown();}};
        server.SetConnectionCallback([](const auto&c){if(c&&c->IsConnected())c->SetHighWaterMarkCallback([](const auto &p,std::size_t){p->ForceClose();},1024*1024);});
        server.SetMessageCallback([&](const auto&c,tinyimx::Buffer*b){auto decoded=codec.Decode(b);if(decoded.status==tinyimx::mcp::HttpDecodeStatus::kNeedMoreData)return;if(decoded.status!=tinyimx::mcp::HttpDecodeStatus::kComplete){reply(c,400,{{"success",false},{"error","invalid HTTP request"}});return;}
            const auto submitted=workers.TrySubmit([&,c,request=std::move(decoded.request)]{
                try{
                    if(request.method=="GET"&&request.target=="/health"){reply(c,200,{{"success",true},{"service","desktop-file"}});return;}
                    if(request.method!="POST"||request.target!="/desktop"||request.Header("content-type").rfind("application/json",0)!=0)throw Failure{400,"POST /desktop application/json required"};
                    if(!request.Header("origin").empty())throw Failure{403,"browser origins denied"};
                    const auto peer=c->PeerAddress().ToString();auto value=ingress.handle(Json::parse(request.body),request.Header("authorization"),peer.substr(0,peer.rfind(':')));value["success"]=true;reply(c,200,std::move(value));
                }catch(const Failure&f){reply(c,f.status,{{"success",false},{"error",f.message}});}catch(const std::exception&){reply(c,400,{{"success",false},{"error","invalid request"}});}
            });if(submitted!=tinyimx::TaskPushResult::kOk)reply(c,503,{{"success",false},{"error","file ingress overloaded"}});
        });
        if(!server.Start())return 2;loop.Loop();server.Stop();workers.Shutdown(tinyimx::ShutdownMode::kGraceful,std::chrono::seconds(10));tinyimx::Logger::Instance().Shutdown();return 0;
    }catch(const std::exception&){std::cerr<<"desktop file ingress startup failed\n";return 2;}
}
