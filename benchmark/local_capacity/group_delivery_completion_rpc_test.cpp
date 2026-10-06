// Real fresh owned MySQL only. All rows, triggers and raw samples are retained.
#include "common/db/MySqlConnectionPool.h"
#include "services/repository/MessageRepository.h"
#include <mysql/mysql.h>
#include <nlohmann/json.hpp>
#include <barrier>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <thread>
namespace {
std::atomic<int> updates{0},pings{0},query_fault{0};
int checks=0;
void Check(bool yes,const char* name) {
    if(!yes)throw std::runtime_error(name);
    ++checks;std::cout<<"[PASS] "<<name<<'\n';
}
}
extern "C" int __real_mysql_query(MYSQL*,const char*);
extern "C" int __real_mysql_ping(MYSQL*);
extern "C" int __wrap_mysql_ping(MYSQL* db){++pings;return __real_mysql_ping(db);}
extern "C" int __wrap_mysql_query(MYSQL* db,const char* text) {
    std::string_view sql(text);
    bool completion=sql.starts_with("UPDATE im_group_message_deliveries SET delivery_status=")||
                    sql.starts_with("UPDATE im_group_message_deliveries AS d JOIN (");
    if(completion) {
        ++updates;int fault=query_fault.exchange(0);
        if(fault==1)return __real_mysql_query(db,"SELECT * FROM own_missing_group_completion_table");
        int code=__real_mysql_query(db,text);
        return fault==2&&code==0?1:code;
    }
    return __real_mysql_query(db,text);
}
namespace {
using namespace tinyimx;
class Fixture {
public:
    MySqlConnectionPool pool;MessageRepository repo{&pool};
    void Exec(const std::string& sql) {
        auto c=pool.Acquire();if(!c||!c->Execute(sql))throw std::runtime_error("OWN_FIXTURE_SQL_ERROR");
    }
    std::uint64_t Count(std::uint64_t mid,const std::string& filter) {
        auto c=pool.Acquire();MySqlQueryResult q;
        if(!c||!c->Query("SELECT COUNT(*) FROM im_group_message_deliveries WHERE message_id="+
            std::to_string(mid)+" AND "+filter,&q))throw std::runtime_error("OWN_FIXTURE_READ_ERROR");
        return std::stoull(q.rows.at(0).at(0));
    }
    void Message(std::uint64_t mid,const std::vector<std::uint64_t>& users,int status=1) {
        auto id=std::to_string(mid);
        Exec("INSERT INTO im_group_messages(message_id,client_message_id,group_id,from_user_id,message_type,"
             "content,membership_epoch,member_version,authorized_role) VALUES ("+id+
             ",'complete-"+id+"',9001,10001,1,'exact own bytes',1,1,1)");
        std::string rows;
        for(auto uid:users){if(!rows.empty())rows+=",";rows+="("+id+","+std::to_string(uid)+",9001,"+std::to_string(status)+")";}
        Exec("INSERT INTO im_group_message_deliveries(message_id,recipient_user_id,group_id,delivery_status) VALUES "+rows);
    }
    std::vector<GroupDeliveryAttemptCompletion> Claim(std::uint64_t mid,std::size_t limit=256) {
        auto x=repo.ClaimGroupMessageDeliveries("owned-coordinator","token'\\bytes",limit,5000,mid);
        if(!x.Succeeded())throw std::runtime_error("OWN_CLAIM_FAILED");
        std::vector<GroupDeliveryAttemptCompletion> out;
        for(auto& row:x.records)out.push_back({row.delivery.message_id,row.delivery.recipient_user_id,row.delivery.lease_token,
            GroupDeliveryStatus::kPending,"owned'\\gateway",3000,""});
        return out;
    }
    GroupDeliveryMutationResult Complete(const std::vector<GroupDeliveryAttemptCompletion>& rows,bool batch) {
        if(batch)return repo.CompleteGroupMessageDeliveryAttempts(rows);
        GroupDeliveryMutationResult sum;sum.status=MessageMutationStatus::kSucceeded;
        for(const auto& row:rows) {
            auto x=repo.CompleteGroupMessageDeliveryAttempt(row.message_id,row.recipient_user_id,row.lease_token,
                row.next_status,row.gateway_id,row.retry_after_ms,row.error_code);
            if(!x.Succeeded())return x;sum.affected_rows+=x.affected_rows;
        }
        return sum;
    }
};
}

#include "services/message/repository/MessageRepositoryAdapter.h"
#include "services/message/application/MessageApplicationService.h"
#include "services/message/service/MessageServiceImpl.h"
#include "services/rpc/MessageRpcClient.h"
#include <grpcpp/grpcpp.h>
namespace {
class Endpoint final:public tinyimx::rpc::ServiceEndpointProvider {
public:
    mutable int calls=0;
    std::string target;
    std::optional<tinyimx::rpc::ServiceEndpoint> Resolve(tinyimx::rpc::ServiceKind kind)const override {
        ++calls;
        if(kind!=tinyimx::rpc::ServiceKind::kMessage||target.empty())return std::nullopt;
        return tinyimx::rpc::ServiceEndpoint{target};
    }
};
struct OwnServer {
    std::unique_ptr<grpc::Server> server;
    ~OwnServer(){if(server){server->Shutdown();server->Wait();}}
};
}
int main(int argc,char**argv) {
try {
    if(argc!=2)return 2;
    std::ifstream in(argv[1]);nlohmann::json cfg;in>>cfg;auto j=cfg.at("mysql");
    MySqlConfig db;db.enable=true;db.host=j.at("host");db.port=j.at("port");
    db.database=j.at("database");db.user=j.at("user");db.password=j.at("password");db.pool_size=j.at("pool_size");
    if(!db.database.starts_with("codex_group_complete_20261006_rpc_"))throw std::runtime_error("REFUSE_NONOWNED_RPC_SCHEMA");
    Fixture f;Check(f.pool.Initialize(db),"owned_rpc_pool_ready");
    std::string users;
    for(std::uint64_t u=10001;u<=10270;++u){if(!users.empty())users+=",";users+="("+std::to_string(u)+",'rpc-"+std::to_string(u)+"','Owned',1)";}
    users+=",(18446744073709551614,'rpc-high','Owned',1)";
    f.Exec("INSERT INTO im_users(user_id,username,nickname,status) VALUES "+users);
    f.Exec("INSERT INTO im_groups(group_id,name,owner_user_id,max_members) VALUES (9001,'Owned RPC completion',10001,500)");
    tinyimx::message::MessageRepositoryAdapter adapter(&f.repo);
    tinyimx::message::MessageApplicationService app(&adapter);
    tinyimx::message::MessageServiceImpl impl(&app);
    grpc::ServerBuilder builder;int port=0;
    builder.AddListeningPort("127.0.0.1:0",grpc::InsecureServerCredentials(),&port);
    builder.RegisterService(&impl);OwnServer own;own.server=builder.BuildAndStart();
    Check(own.server&&port>0,"own_loopback_rpc_server_ready");
    Endpoint endpoint;endpoint.target="127.0.0.1:"+std::to_string(port);
    tinyimx::rpc::MessageRpcClient client(&endpoint);
    tinyimx::rpc::RpcCallOptions options;options.remaining_timeout=std::chrono::milliseconds(5000);
    options.caller_service="native-test";options.caller_instance="owned-rpc";
    using Entry=tinyimx::rpc::CompleteGroupMessageDeliveryAttemptRpcRequest;
    const auto claim=[&](std::uint64_t mid,std::size_t count=256) {
        auto rows=f.Claim(mid,count);std::vector<Entry> entries;
        for(const auto& row:rows) {
            Entry entry;entry.message_id=row.message_id;entry.recipient_user_id=row.recipient_user_id;
            entry.lease_token=row.lease_token;entry.gateway_id=row.gateway_id;entry.retry_after_ms=row.retry_after_ms;
            entry.outcome=tinyimx::rpc::GroupDeliveryAttemptRpcOutcome::kSubmitted;
            entries.push_back(entry);
        }
        return entries;
    };
    f.Message(9501,{10002,10003,10004});auto entries=claim(9501);
    entries[1].outcome=tinyimx::rpc::GroupDeliveryAttemptRpcOutcome::kOffline;
    entries[1].gateway_id="";entries[1].retry_after_ms=0;entries[1].error_code="offline'\\bytes";
    entries[2].outcome=tinyimx::rpc::GroupDeliveryAttemptRpcOutcome::kRetryableFailure;
    entries[2].retry_after_ms=1000;entries[2].error_code="retry";
    updates=pings=0;auto basic=client.CompleteGroupMessageDeliveryAttempts(entries,options);
    int u=updates,p=pings;
    Check(basic.ok()&&basic.attempted&&basic.value->affected_rows==3&&u==1&&p==1,"RPC_app_adapter_repo_one_UPDATE_one_ping");
    auto got=f.repo.FindGroupMessageDelivery(9501,10003);
    Check(got.Found()&&got.record.delivery.delivery_status==GroupDeliveryStatus::kDeferredOffline&&
        got.record.delivery.last_gateway_id.empty()&&got.record.delivery.last_error_code==entries[1].error_code,
        "RPC_mixed_outcome_escaped_bytes_NULL_mapping");
    Check(f.Count(9501,"recipient_user_id=10002 AND next_retry_at>DATE_ADD(NOW(3),INTERVAL 2000000 MICROSECOND)")==1,
        "RPC_original_retry_budget_preserved");
    f.Message(9502,{10002,10003});auto fenced=claim(9502);
    Check(f.repo.ConfirmGroupMessageDelivery(9502,10002).Succeeded(),"owned_RPC_ACK_fixture");
    auto result=client.CompleteGroupMessageDeliveryAttempts(fenced,options);
    Check(result.ok()&&result.value->affected_rows==1&&f.Count(9502,"delivery_status=3")==1,"RPC_ACK_winner_not_reverted");
    result=client.CompleteGroupMessageDeliveryAttempts(fenced,options);
    Check(result.ok()&&result.value->affected_rows==0,"RPC_zero_affected_is_success");
    auto absent=fenced[0];absent.message_id=999999;
    result=client.CompleteGroupMessageDeliveryAttempts({absent},options);
    Check(result.ok()&&result.value->affected_rows==0,"RPC_missing_identity_noop");
    updates=pings=0;int discovery=endpoint.calls;
    const auto rejected=[&](const std::vector<Entry>& v) {
        auto x=client.CompleteGroupMessageDeliveryAttempts(v,options);
        return !x.ok()&&!x.attempted&&x.status.code==tinyimx::rpc::RpcErrorCode::kInvalidArgument;
    };
    Check(rejected({}),"client_empty_rejected");
    Check(rejected({fenced[0],fenced[0]}),"client_duplicate_pair_rejected");
    auto bad=fenced[0];bad.message_id=0;Check(rejected({bad}),"client_zero_identity_rejected");
    bad=fenced[0];bad.lease_token="";Check(rejected({bad}),"client_empty_token_rejected");
    bad=fenced[0];bad.outcome=static_cast<tinyimx::rpc::GroupDeliveryAttemptRpcOutcome>(99);
    Check(rejected({bad}),"client_invalid_outcome_rejected");
    bad=fenced[0];bad.retry_after_ms=600001;Check(rejected({bad}),"client_retry_bound_rejected");
    bool longbad=true;
    for(int i=0;i<3;++i){bad=fenced[0];(i==0?bad.lease_token:i==1?bad.gateway_id:bad.error_code)=std::string(129,'x');longbad=longbad&&rejected({bad});}
    Check(longbad,"client_all_metadata_bounds_rejected");
    std::vector<Entry> over(257,fenced[0]);Check(rejected(over),"client_257_rejected");
    Check(endpoint.calls==discovery&&updates==0&&pings==0,"client_validation_before_discovery_or_SQL");
    auto exhausted=options;exhausted.remaining_timeout=std::chrono::milliseconds(0);
    result=client.CompleteGroupMessageDeliveryAttempts(fenced,exhausted);
    Check(!result.ok()&&!result.attempted&&result.status.code==tinyimx::rpc::RpcErrorCode::kDeadlineExceeded&&endpoint.calls==discovery,
        "client_expired_budget_before_discovery");
    tinyimx::rpc::MessageRpcClient missing(nullptr);
    Check(!missing.CompleteGroupMessageDeliveryAttempts(fenced,options).attempted,"client_missing_provider_no_attempt");
    auto stub=tinyimx::message::v1::MessageService::NewStub(grpc::CreateChannel(endpoint.target,grpc::InsecureChannelCredentials()));
    const auto raw=[&](int variant) {
        tinyimx::message::v1::CompleteGroupMessageDeliveryAttemptsRequest req;
        if(variant!=0) {
            auto* v=req.add_attempts();v->set_message_id(9502);v->set_recipient_user_id(10002);
            v->set_lease_token("old");v->set_outcome(tinyimx::message::v1::GROUP_DELIVERY_ATTEMPT_OUTCOME_SUBMITTED);
            if(variant==1){auto copy=*v;*req.add_attempts()=copy;}
            if(variant==2)v->set_outcome(static_cast<tinyimx::message::v1::GroupDeliveryAttemptOutcome>(99));
            if(variant==3)v->set_message_id(0);
            if(variant==4)v->set_lease_token("");
            if(variant==5)v->set_gateway_id(std::string(129,'x'));
            if(variant==6)v->set_retry_after_ms(600001);
            if(variant==7){auto copy=*v;for(int i=1;i<257;++i)*req.add_attempts()=copy;}
        }
        grpc::ClientContext c;c.set_deadline(std::chrono::system_clock::now()+std::chrono::seconds(5));
        tinyimx::message::v1::MessageMutationResponse response;
        return stub->CompleteGroupMessageDeliveryAttempts(&c,req,&response);
    };
    updates=pings=0;bool serverbad=true;
    for(int i=0;i<8;++i)serverbad=serverbad&&raw(i).error_code()==grpc::StatusCode::INVALID_ARGUMENT;
    Check(serverbad&&updates==0&&pings==0,"raw_RPC_8_invalid_cases_before_DB_even_bypassing_client");
    f.Message(9503,{10002,10003});auto fault=claim(9503);
    f.Exec("CREATE TRIGGER own_rpc_fault BEFORE UPDATE ON im_group_message_deliveries FOR EACH ROW BEGIN "
        "IF NEW.message_id=9503 AND NEW.recipient_user_id=10003 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='OWN_RPC_FAULT'; END IF; END");
    result=client.CompleteGroupMessageDeliveryAttempts(fault,options);
    Check(!result.ok()&&result.attempted&&result.status.code==tinyimx::rpc::RpcErrorCode::kUnavailable&&
        f.Count(9503,"lease_token IS NOT NULL")==2,"RPC_actual_midstatement_SQL_failure_atomic_leases_preserved");
    f.Message(9504,{10002,10003});auto uncertain=claim(9504);query_fault=2;
    result=client.CompleteGroupMessageDeliveryAttempts(uncertain,options);
    Check(query_fault==0&&!result.ok()&&result.attempted&&
        f.Count(9504,"lease_token IS NULL")==2,"RPC_real_commit_then_wrapper_error_is_uncertain_not_network_fault");
    result=client.CompleteGroupMessageDeliveryAttempts(uncertain,options);
    Check(result.ok()&&result.value->affected_rows==0,"RPC_uncertain_repeat_same_token_noop");
    f.Message(9007199254740993ULL,{18446744073709551614ULL});
    result=client.CompleteGroupMessageDeliveryAttempts(claim(9007199254740993ULL),options);
    Check(result.ok()&&result.value->affected_rows==1&&
        f.Count(9007199254740993ULL,"recipient_user_id=18446744073709551614 AND lease_token IS NULL")==1,
        "RPC_uint64_exact_beyond_signed_and_double");
    std::vector<std::uint64_t> members;for(std::uint64_t uid=10002;uid<=10258;++uid)members.push_back(uid);
    f.Message(9505,members);auto maximum=claim(9505);
    result=client.CompleteGroupMessageDeliveryAttempts(maximum,options);
    Check(maximum.size()==256&&result.ok()&&result.value->affected_rows==256&&
        f.Count(9505,"recipient_user_id=10258 AND attempt_count=0 AND lease_token IS NULL AND delivery_status=1")==1,
        "RPC_256_bound_and_unclaimed_257th_row_unchanged");
    f.Message(9506,{10002});auto old=claim(9506);
    result=client.CompleteGroupMessageDeliveryAttempt(old[0],options);
    Check(result.ok()&&result.value->affected_rows==1,"old_single_RPC_compatible_after_append");
    f.Message(9507,{10002});auto ack=claim(9507);
    tinyimx::rpc::ConfirmGroupMessageDeliveryRpcRequest confirm;confirm.message_id=9507;confirm.recipient_user_id=10002;
    Check(client.ConfirmGroupMessageDelivery(confirm,options).ok(),"old_ACK_RPC_compatible_after_append");
    result=client.CompleteGroupMessageDeliveryAttempts(ack,options);
    Check(result.ok()&&result.value->affected_rows==0&&f.Count(9507,"delivery_status=3")==1,"old_ACK_then_new_batch_monotonic");
    Check(!tinyimx::message::MessageApplicationService(nullptr).CompleteGroupMessageDeliveryAttempts({}).Succeeded(),
        "application_invalid_request_missing_repository_rejected");
    own.server->Shutdown();own.server->Wait();own.server.reset();
    auto transport=options;transport.remaining_timeout=std::chrono::milliseconds(150);
    result=client.CompleteGroupMessageDeliveryAttempts(fenced,transport);
    Check(!result.ok()&&result.attempted,"actual_closed_loopback_transport_is_attempted_uncertain");
    std::cout<<nlohmann::json({{"status","GROUP_COMPLETION_RPC_REAL_MYSQL_PASS"},{"checks",checks},
        {"pool",db.pool_size},{"performance_acceptance",false},{"runtime_deploy",false}}).dump()<<'\n';
    f.pool.Shutdown();return 0;
}catch(const std::exception& x){std::cerr<<"[FAIL] "<<x.what()<<'\n';return 1;}
}
