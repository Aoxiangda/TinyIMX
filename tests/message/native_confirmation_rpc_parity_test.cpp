// Compare the public Message RPC contract with confirmation batching off/on.
// This gate deliberately does not claim Gateway authentication, SQL identity,
// RocketMQ/Redis convergence, group/file permissions, or full-system capacity.
#include "services/message/local/LocalMessageRepositoryAdapter.h"
#include "services/message/local/LocalUnreadProjectionSource.h"
#include "services/message/application/MessageApplicationService.h"
#include "services/message/service/MessageServiceImpl.h"
#include "services/projection/unread/UnreadSnapshotRpc.h"
#include "tests/message/ReadyBatchNoopRepository.h"
#include <grpcpp/grpcpp.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace tinyimx::message;
using namespace tinyimx::message::local;
using namespace tinyimx::projection::unread;
using Json=nlohmann::json;
void Check(bool ok,const char* label){if(!ok)throw std::runtime_error(label);std::cout<<"PASS "<<label<<'\n';}
struct Wire {
    std::unique_ptr<v1::MessageService::Stub> stub;
    v1::PersistPrivateMessageResponse Persist(const std::string& cid,const std::string& text) {
        v1::PersistPrivateMessageRequest q;q.set_from_user_id(1);q.set_to_user_id(2);
        q.set_client_message_id(cid);q.set_message_type(1);q.set_content(text);
        v1::PersistPrivateMessageResponse r;grpc::ClientContext c;
        c.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));
        Check(stub->PersistPrivateMessage(&c,q,&r).ok(),"persist transport reaches actual service");return r;
    }
    std::pair<int,std::uint64_t> Confirm(std::uint64_t mid,std::uint64_t receiver) {
        v1::ConfirmReceiverRequest q;q.set_message_id(mid);q.set_receiver_user_id(receiver);
        v1::MessageMutationResponse r;grpc::ClientContext c;
        c.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));
        const auto s=stub->ConfirmReceiver(&c,q,&r);return{int(s.error_code()),r.affected_rows()};
    }
    std::uint64_t Read() {
        v1::MarkDialogReadRequest q;q.set_reader_user_id(2);q.set_peer_user_id(1);
        v1::MessageMutationResponse r;grpc::ClientContext c;
        c.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));
        Check(stub->MarkDialogRead(&c,q,&r).ok(),"mark-read transport reaches actual service");return r.affected_rows();
    }
    std::vector<std::uint64_t> History(std::uint64_t actor,std::uint64_t peer) {
        v1::ListHistoryRequest q;q.set_actor_user_id(actor);q.set_peer_user_id(peer);q.set_limit(50);
        v1::ListHistoryResponse r;grpc::ClientContext c;
        c.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(3));
        Check(stub->ListHistory(&c,q,&r).ok(),"history transport reaches actual service");
        std::vector<std::uint64_t> mids;for(const auto& m:r.messages())mids.push_back(m.message_id());
        Check(!r.has_more()&&std::is_sorted(mids.begin(),mids.end()),"history is bounded ordered complete page");return mids;
    }
};
Json Arm(const std::filesystem::path& root,bool enabled) {
    std::filesystem::create_directory(root);
    LocalPrivateStoreOptions options;options.directory=(root/"store").string();
    options.storage_identity="perf864-rpc-contract";options.message_events_topic="codex_perf864_events_20261024";
    options.first_message_id=7098640000000000ULL;options.initial_user_ids={1,2,3,4};
    options.block_cache_bytes=8*1024*1024;options.exact_user_locking=true;
    Json result=Json::object();std::vector<std::uint64_t> mids;
    {
        LocalPrivateStore store(options);tinyimx::tests::ReadyBatchNoopRepository groups;
        LocalMessageRepositoryAdapter repository(&store,&groups,enabled);MessageApplicationService app(&repository);
        MessageServiceImpl service(&app);LocalUnreadProjectionSource source(&store);UnreadSnapshotRpcService snapshot_service(&source);
        grpc::ServerBuilder b;int port=0;b.AddListeningPort("127.0.0.1:0",grpc::InsecureServerCredentials(),&port);
        b.RegisterService(&service);b.RegisterService(&snapshot_service);auto server=b.BuildAndStart();
        Check(server&&port,"owned real loopback Message RPC server starts");
        const auto target="127.0.0.1:"+std::to_string(port);
        auto channel=grpc::CreateChannel(target,grpc::InsecureChannelCredentials());
        Check(channel->WaitForConnected(std::chrono::system_clock::now()+std::chrono::seconds(3)),"client connects to actual service");
        Wire wire{v1::MessageService::NewStub(channel)};UnreadSnapshotRpcClient snapshots(target,std::chrono::milliseconds(500));
        auto first=wire.Persist("same-cid","original content");mids.push_back(first.message_id());
        Check(first.result()==v1::PERSIST_PRIVATE_MESSAGE_RESULT_CREATED&&first.message_id()==options.first_message_id,
            "first success has durable original MID");
        auto retry=wire.Persist("same-cid","original content");
        Check(retry.result()==v1::PERSIST_PRIVATE_MESSAGE_RESULT_REUSED&&retry.message_id()==first.message_id(),
            "lost sender ACK retry reuses exact MID");
        auto conflict=wire.Persist("same-cid","conflicting content");
        Check(conflict.result()==v1::PERSIST_PRIVATE_MESSAGE_RESULT_IDEMPOTENCY_CONFLICT&&conflict.message_id()==first.message_id(),
            "same CID changed content is explicit conflict");
        result["persist_results"]={int(first.result()),int(retry.result()),int(conflict.result())};
        result["wrong_recipient"]=wire.Confirm(first.message_id(),3);
        result["unknown_message"]=wire.Confirm(first.message_id()+1000,2);
        Check(result["wrong_recipient"]==Json(std::pair<int,std::uint64_t>{int(grpc::StatusCode::PERMISSION_DENIED),0})&&
            result["unknown_message"]==Json(std::pair<int,std::uint64_t>{int(grpc::StatusCode::NOT_FOUND),0}),
            "wrong recipient and unknown MID fail without mutation");
        Check(store.CountPending(2).count==1,"rejected acknowledgements leave message pending");
        result["confirmed"]=wire.Confirm(first.message_id(),2);result["duplicate_confirmed"]=wire.Confirm(first.message_id(),2);
        Check(result["confirmed"]==Json(std::pair<int,std::uint64_t>{0,1})&&
            result["duplicate_confirmed"]==Json(std::pair<int,std::uint64_t>{0,0}),"receiver confirmation is owned and idempotent");
        auto snapshot=snapshots.LoadDialogSnapshot(2,1);
        Check(snapshot.success&&snapshot.value.private_unread==1&&snapshot.value.total_unread==1,
            "receiver confirmation does not incorrectly mark message read");
        result["read_first"]=wire.Read();result["read_repeat"]=wire.Read();result["confirm_after_read"]=wire.Confirm(first.message_id(),2);
        Check(result["read_first"]==1&&result["read_repeat"]==0&&result["confirm_after_read"]==Json(std::pair<int,std::uint64_t>{0,0}),
            "read and delayed confirmation are monotonic");
        for(int i=0;i<24;++i){auto r=wire.Persist("parallel-"+std::to_string(i),"private body "+std::to_string(i));
            Check(r.result()==v1::PERSIST_PRIVATE_MESSAGE_RESULT_CREATED,"concurrent-ack fixture has independent durable message");mids.push_back(r.message_id());}
        std::vector<std::future<std::pair<int,std::uint64_t>>> pending;
        for(std::size_t i=1;i<mids.size();++i)pending.push_back(std::async(std::launch::async,[&,i]{return wire.Confirm(mids[i],2);}));
        for(auto& f:pending)Check(f.get()==std::pair<int,std::uint64_t>{0,1},"real concurrent single-confirm RPC commits exactly one transition");
        Check(store.CountPending(2).count==0,"all concurrent owned confirmations drain pending state");
        snapshot=snapshots.LoadDialogSnapshot(2,1);
        Check(snapshot.success&&snapshot.value.private_unread==24&&snapshot.value.total_unread==24,
            "all confirmed unread messages remain unread");
        result["read_remaining"]=wire.Read();Check(result["read_remaining"]==24,"read accounts for exact unread durable set");
        result["history"]=wire.History(2,1);Check(result["history"]==Json(mids),"history contains every immutable business record exactly once");
        Check(wire.History(3,1).empty(),"unrelated actor receives no private records");
        retry=wire.Persist("same-cid","original content");
        Check(retry.result()==v1::PERSIST_PRIVATE_MESSAGE_RESULT_REUSED&&retry.record().delivery_state()==v1::MESSAGE_DELIVERY_STATE_READ,
            "retry after read retains durable read state");
        result["final_pending"]=store.CountPending(2).count;result["final_unread"]=store.ReadUserUnreadSnapshot(2).total_unread;
        result["event_count"]=store.Stats().pending_events;
        Check(store.Shutdown(),"store drains and closes cleanly with actual RPC service alive");
        const auto closed=wire.Confirm(first.message_id(),2);result["closed_owner"]=closed.first;
        Check(closed.first==int(grpc::StatusCode::UNAVAILABLE)&&!snapshots.LoadUserSnapshot(2).success,
            "closed owner cannot produce successful ACK or empty successful snapshot");
        server->Shutdown();server->Wait();
    }
    {
        LocalPrivateStore recovered(options);Check(recovered.CountPending(2).count==0&&recovered.ReadUserUnreadSnapshot(2).total_unread==0,
            "reopen retains exact pending and unread state");
        for(auto mid:mids){auto m=recovered.GetPrivateMessage(mid);Check(m.Found()&&m.record.delivery_state==MessageDeliveryState::kRead,
            "reopen retains each acknowledged read record");}
        auto history=recovered.ListHistory(2,1,0,50);Check(history.Succeeded()&&history.records.size()==mids.size(),
            "reopen retains entire history without SQL fallback");
        Check(recovered.Shutdown(),"recovered owner closes cleanly");
    }
    return result;
}
}
int main(int argc,char** argv) {
    try{if(argc!=2)return 2;const auto root=std::filesystem::canonical(argv[1]);
        Check(root.parent_path()=="/mnt/tinyimx-benchmark"&&root.filename().string().starts_with("rpc-parity-perf864-"),"finite new-volume fixture boundary");
        const auto off=Arm(root/"off",false);const auto on=Arm(root/"on",true);
        Check(off==on,"all canonical business RPC outcomes and recovered state match with batching off/on");
        std::cout<<"PARITY_TRANSCRIPT "<<off.dump()<<'\n';
        std::cout<<"PRIVATE_CONFIRMATION_REAL_RPC_PARITY_PASS; FULL_GATEWAY_MQ_REDIS_GROUP_FILE_NOT_EVALUATED\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
