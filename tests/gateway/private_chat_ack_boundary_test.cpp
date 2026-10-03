#include "gateway/RemoteDurableAcceptance.h"
#include "gateway/ChatRequestPhaseTrace.h"
#include "gateway/GatewayPeerTransport.h"
#include <atomic>
#include <functional>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

// Edge doubles only: no real gRPC/MySQL/network is used by this component test.
// The branch included below is copied byte-for-byte from patched GatewayServer
// and the source guard verifies that equality before building.
#define LOG_INFO(x) do { std::ostringstream s; s << x; } while (0)
#define LOG_WARN(x) LOG_INFO(x)
#define LOG_ERROR(x) LOG_INFO(x)
#define REQUIRE(x) do { if (!(x)) throw std::runtime_error(#x); } while (0)
using namespace tinyimx;
using namespace std::chrono_literals;

struct FixtureData {
    rpc::PersistPrivateMessageRpcRequest expected{101,102,"unit-c1",1,"{canonical}"};
    rpc::PersistPrivateMessageRpcCallResult call;
    FixtureData() {
        rpc::PersistPrivateMessageRpcResponse r;
        r.message_id=900;
        r.record.message_id=900;
        r.record.client_message_id=expected.client_message_id;
        r.record.from_user_id=expected.from_user_id;
        r.record.to_user_id=expected.to_user_id;
        r.record.message_type=expected.message_type;
        r.record.content=expected.content;
        call=rpc::PersistPrivateMessageRpcCallResult::Success(r);
    }
    std::optional<ClientChatAck> Ack() const {
        return BuildRemoteDurableAcceptance(call,expected,3,8,"gateway-b","127.0.0.1",9102);
    }
};
struct Endpoint { std::string gateway_id{"gateway-b"},listen_host{"127.0.0.1"}; std::uint16_t listen_port{9102}; };
struct FakePeer {
    std::function<void(GatewayPeerTransportResult)> callback;
    std::vector<std::string>* events{};
    bool accept{true}, synchronous{false}; int submissions{0};
    bool ForwardChat(const Endpoint&, std::uint64_t,std::uint64_t,std::uint64_t,
                     const std::string&,std::function<void(GatewayPeerTransportResult)> cb) {
        ++submissions; events->push_back("submit"); callback=std::move(cb);
        if (synchronous) Complete(true);
        return accept;
    }
    void Complete(bool delivered) {
        GatewayPeerTransportResult r;
        r.status=delivered?GatewayPeerTransportStatus::kOk:GatewayPeerTransportStatus::kRequestTimeout;
        r.response.status=delivered?GatewayForwardChatStatus::kDelivered:GatewayForwardChatStatus::kInternalError;
        events->push_back("peer-complete"); if(callback) callback(r);
    }
};
struct BranchHarness : FixtureData {
    std::vector<ClientChatAck>* replies;
    std::vector<std::string>* events;
    FakePeer* gateway_peer_transport_manager_;
    bool handoff{true};
    void Execute() {
        const auto from_user_id=expected.from_user_id, to_user_id=expected.to_user_id;
        const auto server_message_id=call.value?call.value->message_id:0;
        const auto client_request_reused=call.value && call.value->Reused();
        const auto& persist_call=call;
        const auto& server_body_text=expected.content;
        ClientChatRequest request; request.client_message_id=expected.client_message_id;
        request.to_user_id=expected.to_user_id;
        Endpoint remote_gateway;
        std::int64_t receiver_private_unread=3, receiver_total_unread=8;
        BusinessRequestContext context;
        ChatRequestPhaseTrace phase_trace(context,"chat",nullptr);
        auto send_client_chat_ack=[&](const ClientChatAck& ack){events->push_back("ack");replies->push_back(ack);return handoff;};
#include "private_chat_remote_branch.inc"
    }
};

static ChatRequestPhaseTrace::Snapshot trace_output;
static int trace_calls=0;
static void Capture(const ChatRequestPhaseTrace::Snapshot& s) noexcept { trace_output=s; ++trace_calls; }

int main() {
    int passed=0, failed=0;
    auto test=[&](const char* name, auto fn){try{fn();++passed;std::cout<<"PASS "<<name<<"\n";}catch(const std::exception&e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<"\n";}};
    test("created_pending_is_durable_not_delivered",[]{FixtureData x;auto a=x.Ack();REQUIRE(a&&a->success&&a->stored_persistent&&!a->delivered&&!a->reused);REQUIRE(a->message_id==900&&a->client_message_id=="unit-c1");});
    test("reused_confirmed_and_read_are_true",[]{for(auto s:{rpc::MessageDeliveryState::kReceiverConfirmed,rpc::MessageDeliveryState::kRead}){FixtureData x;x.call.value->outcome=rpc::PersistPrivateMessageRpcOutcome::kReused;x.call.value->record.delivery_state=s;auto a=x.Ack();REQUIRE(a&&a->delivered&&a->reused);}});
    test("reused_failed_never_claims_delivery",[]{FixtureData x;x.call.value->outcome=rpc::PersistPrivateMessageRpcOutcome::kReused;x.call.value->record.delivery_state=rpc::MessageDeliveryState::kFailed;auto a=x.Ack();REQUIRE(a&&a->success&&!a->delivered);});
    test("uncertain_cannot_be_success",[]{FixtureData x;x.call=rpc::PersistPrivateMessageRpcCallResult::Failure(rpc::RpcErrorCode::kDeadlineExceeded,"",true);REQUIRE(!x.Ack());});
    test("not_attempted_cannot_be_success",[]{FixtureData x;x.call=rpc::PersistPrivateMessageRpcCallResult::Failure(rpc::RpcErrorCode::kDeadlineExceeded,"",false);REQUIRE(!x.Ack());});
    test("conflict_cannot_be_success",[]{FixtureData x;x.call.value->outcome=rpc::PersistPrivateMessageRpcOutcome::kIdempotencyConflict;REQUIRE(!x.Ack());});
    test("zero_message_and_response_mismatch_rejected",[]{FixtureData x;x.call.value->message_id=0;REQUIRE(!x.Ack());x=FixtureData{};x.call.value->record.message_id=901;REQUIRE(!x.Ack());});
    test("sender_receiver_cid_type_content_mismatch_rejected",[]{FixtureData x; x.call.value->record.from_user_id++;REQUIRE(!x.Ack());x=FixtureData{};x.call.value->record.to_user_id++;REQUIRE(!x.Ack());x=FixtureData{};x.call.value->record.client_message_id+="x";REQUIRE(!x.Ack());x=FixtureData{};x.call.value->record.message_type++;REQUIRE(!x.Ack());x=FixtureData{};x.call.value->record.content+="x";REQUIRE(!x.Ack());});
    test("created_confirmed_response_rejected",[]{FixtureData x;x.call.value->record.delivery_state=rpc::MessageDeliveryState::kReceiverConfirmed;REQUIRE(!x.Ack());});
    test("snapshot_fields_retained",[]{FixtureData x;auto a=x.Ack();REQUIRE(a->receiver_private_unread==3&&a->receiver_total_unread==8);REQUIRE(a->remote_gateway_id=="gateway-b"&&a->remote_port==9102);});
    test("production_branch_ack_precedes_delayed_callback",[]{std::vector<std::string> order;std::vector<ClientChatAck> replies;FakePeer p;p.events=&order;BranchHarness h;h.events=&order;h.replies=&replies;h.gateway_peer_transport_manager_=&p;h.Execute();REQUIRE(order==std::vector<std::string>({"ack","submit"}));REQUIRE(replies.size()==1&&replies[0].success);p.Complete(true);REQUIRE(replies.size()==1);});
    test("production_branch_sync_callback_no_second_ack",[]{std::vector<std::string> order;std::vector<ClientChatAck> replies;FakePeer p;p.events=&order;p.synchronous=true;BranchHarness h;h.events=&order;h.replies=&replies;h.gateway_peer_transport_manager_=&p;h.Execute();REQUIRE(order.front()=="ack"&&replies.size()==1);});
    test("production_branch_rejected_submit_no_second_ack",[]{std::vector<std::string> order;std::vector<ClientChatAck> replies;FakePeer p;p.events=&order;p.accept=false;BranchHarness h;h.events=&order;h.replies=&replies;h.gateway_peer_transport_manager_=&p;h.Execute();REQUIRE(replies.size()==1&&replies[0].success);p.Complete(false);REQUIRE(replies.size()==1);});
    test("sender_handoff_failure_does_not_cancel_forward",[]{std::vector<std::string> order;std::vector<ClientChatAck> replies;FakePeer p;p.events=&order;BranchHarness h;h.events=&order;h.replies=&replies;h.gateway_peer_transport_manager_=&p;h.handoff=false;h.Execute();REQUIRE(p.submissions==1&&replies.size()==1);});
    test("callback_outlives_gateway_no_sender_capture",[]{std::vector<std::string> order;std::vector<ClientChatAck> replies;FakePeer p;p.events=&order;{BranchHarness h;h.events=&order;h.replies=&replies;h.gateway_peer_transport_manager_=&p;h.Execute();}p.Complete(true);p.Complete(false);REQUIRE(replies.size()==1);});
    test("invalid_identity_never_forwards",[]{std::vector<std::string> order;std::vector<ClientChatAck> replies;FakePeer p;p.events=&order;BranchHarness h;h.events=&order;h.replies=&replies;h.gateway_peer_transport_manager_=&p;h.call.value->record.to_user_id++;h.Execute();REQUIRE(p.submissions==0&&replies.size()==1&&!replies[0].success);});
    test("callbacks_do_not_mutate_durable_confirmation",[]{std::vector<std::string> order;std::vector<ClientChatAck> replies;FakePeer p;p.events=&order;BranchHarness h;h.events=&order;h.replies=&replies;h.gateway_peer_transport_manager_=&p;h.Execute();p.Complete(true);REQUIRE(!replies[0].delivered);REQUIRE(h.call.value->record.delivery_state==rpc::MessageDeliveryState::kPending);});
    test("same_logical_id_new_request_has_own_response",[]{std::vector<std::string> order;std::vector<ClientChatAck> replies;FakePeer p;p.events=&order;BranchHarness h;h.events=&order;h.replies=&replies;h.gateway_peer_transport_manager_=&p;h.Execute();h.call.value->outcome=rpc::PersistPrivateMessageRpcOutcome::kReused;h.Execute();REQUIRE(replies.size()==2&&replies[1].reused&&replies[0].message_id==replies[1].message_id);});
    test("phase_trace_queue_age_not_deadline_reset",[]{BusinessRequestContext r;const auto now=BusinessClock::now();r.received_at=now-3500ms;r.deadline=now-500ms;r.user_id=101;r.request_seq=7;auto original=r.deadline;trace_calls=0;{ChatRequestPhaseTrace t(r,"chat",Capture,now);t.Fail();REQUIRE(t.Current().dispatch_age_us==3500000);REQUIRE(t.Current().entry_budget_us==-500000);REQUIRE(t.Measure(ChatRequestPhaseTrace::Phase::Permission,[]{return 3;})==3);}REQUIRE(r.deadline==original&&trace_calls==1);REQUIRE(trace_output.duration_us[0]>=0&&trace_output.duration_us[2]==-1);});
    test("phase_trace_void_exception_and_peer",[]{BusinessRequestContext r;r.deadline=BusinessClock::now()+1s;trace_calls=0;{ChatRequestPhaseTrace t(r,"peer",Capture);t.SetMessageId(900);t.Measure(ChatRequestPhaseTrace::Phase::Unread,[]{});try{t.Measure(ChatRequestPhaseTrace::Phase::PeerValidate,[]{throw std::runtime_error("test");});}catch(const std::runtime_error&){} }REQUIRE(trace_calls==1&&trace_output.failed&&trace_output.message_id==900&&trace_output.duration_us[4]>=0);});
    test("fast_trace_is_suppressed",[]{BusinessRequestContext r;trace_calls=0;{ChatRequestPhaseTrace t(r,"chat",Capture);}REQUIRE(trace_calls==0);});
    test("phase_log_limiter_is_concurrent_and_bounded",[]{ChatPhaseLogLimiter l;std::atomic<int> n{0};std::vector<std::thread> ts;for(int i=0;i<32;i++)ts.emplace_back([&]{if(l.Admit(100))n++;});for(auto&t:ts)t.join();REQUIRE(n==8&&l.TakeSuppressed()==24);REQUIRE(l.Admit(101));REQUIRE(!l.Admit(100));});
    std::cout<<"R2A_COMPONENT_TESTS total="<<(passed+failed)<<" passed="<<passed<<" failed="<<failed<<"\n";
    std::cout<<"FULL_GATEWAY_BUILD=NOT_EVALUATED_BY_COMPONENT_TEST; MYSQL_E2E=NOT_RUN; ORIGINAL_10K=FAIL\n";
    return failed?1:0;
}
