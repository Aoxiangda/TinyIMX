#include "services/projection/unread/UnreadSnapshotRpc.h"
#include "common/observability/SnapshotRpcTiming.h"
#include <grpcpp/grpcpp.h>
#include <limits>
#include <stdexcept>
namespace tinyimx::projection::unread {
namespace {
constexpr std::size_t kMaxPeers=100000,kMaxBytes=2*1024*1024;
using SnapshotTiming=diagnostics::SnapshotRpcTiming;
SnapshotTiming::Key TimingKey(grpc::ServerContext* context)noexcept {
    if(!SnapshotTiming::Enabled()||!context)return{};
    const auto range=context->client_metadata().equal_range(SnapshotTiming::MetadataHeader);
    if(range.first==range.second)return{};
    auto next=range.first;++next;
    if(next!=range.second)return{}; // Duplicate/untrusted tags never affect RPC status.
    const auto&value=range.first->second;
    return SnapshotTiming::ParseKey(std::string_view(value.data(),value.size()));
}
void AddTimingKey(SnapshotTiming& trace,grpc::ClientContext& context)noexcept {
    if(!trace.Active())return;
    try{context.AddMetadata(SnapshotTiming::MetadataHeader,SnapshotTiming::EncodeKey(trace.CurrentKey()));}
    catch(...){trace.ForgetKey();} // Diagnostic allocation failure cannot fail a read.
}
bool Valid(const DialogUnreadSnapshot&s,std::uint64_t receiver,std::uint64_t peer){
    return s.receiver_user_id==receiver&&s.peer_user_id==peer&&s.private_unread>=0&&s.total_unread>=s.private_unread;
}
bool Valid(const UserUnreadSnapshot&s,std::uint64_t receiver){
    if(s.receiver_user_id!=receiver||s.total_unread<0||s.peer_counts.size()>kMaxPeers)return false;
    std::uint64_t previous=0;std::int64_t total=0;
    for(const auto&[peer,count]:s.peer_counts){
        if(!peer||peer==receiver||peer<=previous||count<0||count>std::numeric_limits<std::int64_t>::max()-total)return false;
        previous=peer;total+=count;
    }
    return total==s.total_unread;
}
grpc::Status Unavailable(){return {grpc::StatusCode::UNAVAILABLE,"durable unread owner unavailable"};}
}
UnreadSnapshotRpcService::UnreadSnapshotRpcService(UnreadProjectionSource* source):source_(source){if(!source)throw std::invalid_argument("snapshot RPC needs durable source");}
grpc::Status UnreadSnapshotRpcService::ReadDialogUnreadSnapshot(grpc::ServerContext*c,const message::v1::ReadDialogUnreadSnapshotRequest*q,message::v1::ReadDialogUnreadSnapshotResponse*r){
    SnapshotTiming trace(SnapshotTiming::Server,SnapshotTiming::Dialog,q?q->receiver_user_id():0,q?q->peer_user_id():0,1,TimingKey(c));
    const auto status=[&]()->grpc::Status{
    if(!c||!q||!r||!q->receiver_user_id()||!q->peer_user_id()||q->receiver_user_id()==q->peer_user_id())return{grpc::StatusCode::INVALID_ARGUMENT,"invalid unread dialog"};
    if(c->IsCancelled())return{grpc::StatusCode::CANCELLED,"snapshot cancelled"};
    try{
        auto result=trace.Measure([&]{return source_->LoadDialogSnapshot(q->receiver_user_id(),q->peer_user_id());});
        if(!result.success)return Unavailable();
        if(!Valid(result.value,q->receiver_user_id(),q->peer_user_id()))return{grpc::StatusCode::DATA_LOSS,"invalid durable dialog snapshot"};
        if(c->IsCancelled())return{grpc::StatusCode::CANCELLED,"snapshot cancelled"};
        r->set_receiver_user_id(result.value.receiver_user_id);r->set_peer_user_id(result.value.peer_user_id);
        r->set_private_unread(result.value.private_unread);r->set_total_unread(result.value.total_unread);return grpc::Status::OK;
    }catch(const std::exception&){return Unavailable();}
    }();
    trace.Result(static_cast<int>(status.error_code()));if(status.ok())trace.ResponseValid();
    return status;
}
grpc::Status UnreadSnapshotRpcService::ReadUserUnreadSnapshot(grpc::ServerContext*c,const message::v1::ReadUserUnreadSnapshotRequest*q,message::v1::ReadUserUnreadSnapshotResponse*r){
    SnapshotTiming trace(SnapshotTiming::Server,SnapshotTiming::User,q?q->receiver_user_id():0,0,1,TimingKey(c));
    const auto status=[&]()->grpc::Status{
    if(!c||!q||!r||!q->receiver_user_id())return{grpc::StatusCode::INVALID_ARGUMENT,"invalid unread user"};
    if(c->IsCancelled())return{grpc::StatusCode::CANCELLED,"snapshot cancelled"};
    try{
        auto result=trace.Measure([&]{return source_->LoadUserSnapshot(q->receiver_user_id());});if(!result.success)return Unavailable();
        if(!Valid(result.value,q->receiver_user_id()))return{grpc::StatusCode::DATA_LOSS,"invalid complete durable user snapshot"};
        if(c->IsCancelled())return{grpc::StatusCode::CANCELLED,"snapshot cancelled"};
        r->set_receiver_user_id(result.value.receiver_user_id);r->set_total_unread(result.value.total_unread);
        for(const auto&[peer,count]:result.value.peer_counts){auto* row=r->add_peers();row->set_peer_user_id(peer);row->set_unread(count);}
        if(r->ByteSizeLong()>kMaxBytes){r->Clear();return{grpc::StatusCode::RESOURCE_EXHAUSTED,"complete snapshot byte budget exceeded"};}
        return grpc::Status::OK;
    }catch(const std::exception&){return Unavailable();}
    }();
    trace.Result(static_cast<int>(status.error_code()));if(status.ok())trace.ResponseValid();
    return status;
}
UnreadSnapshotRpcClient::UnreadSnapshotRpcClient(const std::string& target,std::chrono::milliseconds timeout):timeout_(timeout){
    if(target.empty()||timeout.count()<1||timeout.count()>5000)throw std::invalid_argument("snapshot endpoint/timeout invalid");
    grpc::ChannelArguments args;args.SetMaxReceiveMessageSize(kMaxBytes);args.SetMaxSendMessageSize(1024);
    stub_=message::v1::UnreadSnapshotService::NewStub(grpc::CreateCustomChannel(target,grpc::InsecureChannelCredentials(),args));
}
UnreadProjectionReadResult<DialogUnreadSnapshot> UnreadSnapshotRpcClient::LoadDialogSnapshot(std::uint64_t receiver,std::uint64_t peer){
    SnapshotTiming trace(SnapshotTiming::Client,SnapshotTiming::Dialog,receiver,peer,1,{});
    UnreadProjectionReadResult<DialogUnreadSnapshot> out;
    if(!receiver||!peer||receiver==peer){out.message="invalid unread dialog";return out;}
    grpc::ClientContext c;AddTimingKey(trace,c);c.set_deadline(std::chrono::system_clock::now()+timeout_);c.set_wait_for_ready(false);
    message::v1::ReadDialogUnreadSnapshotRequest q;q.set_receiver_user_id(receiver);q.set_peer_user_id(peer);message::v1::ReadDialogUnreadSnapshotResponse r;
    const auto status=trace.Measure([&]{return stub_->ReadDialogUnreadSnapshot(&c,q,&r);});trace.Result(static_cast<int>(status.error_code()));
    if(!status.ok()){out.message="durable owner RPC failed: "+std::to_string(status.error_code());return out;}
    DialogUnreadSnapshot value{r.receiver_user_id(),r.peer_user_id(),r.private_unread(),r.total_unread()};
    if(!Valid(value,receiver,peer)){out.message="invalid owner dialog response";return out;}
    out.success=true;out.value=value;trace.ResponseValid();return out;
}
UnreadProjectionReadResult<UserUnreadSnapshot> UnreadSnapshotRpcClient::LoadUserSnapshot(std::uint64_t receiver){
    SnapshotTiming trace(SnapshotTiming::Client,SnapshotTiming::User,receiver,0,1,{});
    UnreadProjectionReadResult<UserUnreadSnapshot> out;if(!receiver){out.message="invalid unread user";return out;}
    grpc::ClientContext c;AddTimingKey(trace,c);c.set_deadline(std::chrono::system_clock::now()+timeout_);c.set_wait_for_ready(false);
    message::v1::ReadUserUnreadSnapshotRequest q;q.set_receiver_user_id(receiver);message::v1::ReadUserUnreadSnapshotResponse r;
    const auto status=trace.Measure([&]{return stub_->ReadUserUnreadSnapshot(&c,q,&r);});trace.Result(static_cast<int>(status.error_code()));
    if(!status.ok()){out.message="durable owner RPC failed: "+std::to_string(status.error_code());return out;}
    UserUnreadSnapshot value;value.receiver_user_id=r.receiver_user_id();value.total_unread=r.total_unread();
    if(static_cast<std::size_t>(r.peers_size())>kMaxPeers){out.message="owner peer count over budget";return out;}
    for(const auto&row:r.peers())value.peer_counts.emplace_back(row.peer_user_id(),row.unread());
    if(!Valid(value,receiver)){out.message="invalid complete owner user response";return out;}
    out.success=true;out.value=std::move(value);trace.ResponseValid();return out;
}
}
