// Real fresh owned MySQL only. All rows, triggers and raw samples are retained.
#include "common/db/MySqlConnectionPool.h"
#include "services/repository/MessageRepository.h"
#include <mysql/mysql.h>
#include <nlohmann/json.hpp>
#include <barrier>
#include <chrono>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <thread>
namespace {
thread_local int updates=0,pings=0,query_fault=0;
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
        ++updates;int fault=query_fault;query_fault=0;
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
int main(int argc,char** argv) {
try {
    if(argc!=3)return 2;bool batch=std::string(argv[2])=="1";
    std::ifstream stream(argv[1]);nlohmann::json cfg;stream>>cfg;auto j=cfg.at("mysql");
    MySqlConfig db;db.enable=true;db.host=j.at("host");db.port=j.at("port");db.database=j.at("database");
    db.user=j.at("user");db.password=j.at("password");db.pool_size=j.at("pool_size");
    if(!db.database.starts_with("codex_group_complete_20261006_"))throw std::runtime_error("REFUSE_NONOWNED_SCHEMA");
    Fixture f;Check(f.pool.Initialize(db),"owned_pool_ready");
    std::string users;
    for(std::uint64_t u=10001;u<=10270;++u){if(!users.empty())users+=",";users+="("+std::to_string(u)+",'own-"+std::to_string(u)+"','Owned',1)";}
    users+=",(18446744073709551614,'own-high','Owned high',1)";
    f.Exec("INSERT INTO im_users(user_id,username,nickname,status) VALUES "+users);
    f.Exec("INSERT INTO im_groups(group_id,name,owner_user_id,max_members) VALUES (9001,'Owned completion',10001,500)");
    f.Message(9201,{10002,10003,10004});auto rows=f.Claim(9201);rows[1].next_status=GroupDeliveryStatus::kDeferredOffline;
    rows[1].gateway_id="";rows[1].error_code="offline'\\bytes";rows[1].retry_after_ms=0;
    updates=pings=0;auto basic=f.Complete(rows,batch);int uc=updates,pc=pings;
    Check(basic.Succeeded()&&basic.affected_rows==3,"mixed_completion_exact_affected");
    Check(uc==(batch?1:3)&&pc==(batch?1:3),"one_statement_and_original_health_ping");
    auto got=f.repo.FindGroupMessageDelivery(9201,10002);
    Check(got.Found()&&got.record.delivery.last_gateway_id==rows[0].gateway_id&&
          got.record.delivery.lease_token.empty()&&got.record.delivery.lease_owner.empty(),"exact_escaped_gateway_and_lease_release");
    got=f.repo.FindGroupMessageDelivery(9201,10003);
    Check(got.Found()&&got.record.delivery.delivery_status==GroupDeliveryStatus::kDeferredOffline&&
          got.record.delivery.last_gateway_id.empty()&&got.record.delivery.last_error_code==rows[1].error_code,"offline_null_gateway_and_exact_error");
    Check(f.Count(9201,"recipient_user_id=10002 AND next_retry_at>DATE_ADD(NOW(3),INTERVAL 2000000 MICROSECOND)")==1&&
          f.Count(9201,"recipient_user_id=10003 AND next_retry_at<=NOW(3)")==1,"original_retry_budget_and_offline_zero");
    f.Message(9202,{10002,10003,10004,10005});auto fenced=f.Claim(9202);
    Check(f.repo.ConfirmGroupMessageDelivery(9202,10002).Succeeded(),"ack_fixture_confirmed");
    f.Exec("UPDATE im_group_message_deliveries SET lease_token='replacement-token' WHERE message_id=9202 AND recipient_user_id=10003");
    fenced[3].next_status=GroupDeliveryStatus::kDeferredOffline;auto fence=f.Complete(fenced,batch);
    Check(fence.Succeeded()&&fence.affected_rows==2,"ACK_and_wrong_token_are_successful_noops");
    Check(f.Count(9202,"recipient_user_id=10002 AND delivery_status=3 AND lease_token IS NULL")==1&&
          f.Count(9202,"recipient_user_id=10003 AND lease_token='replacement-token'")==1,"ACK_and_newer_lease_not_overwritten");
    auto again=f.Complete(fenced,batch);Check(again.Succeeded()&&again.affected_rows==0,"repeat_completion_is_noop");
    auto absent=fenced[0];absent.message_id=999999;
    Check(f.repo.CompleteGroupMessageDeliveryAttempts({absent}).Succeeded()&&
          f.repo.CompleteGroupMessageDeliveryAttempts({absent}).affected_rows==0,"missing_identity_no_other_row_changed");
    std::vector<GroupDeliveryAttemptCompletion> empty;updates=pings=0;
    Check(f.repo.CompleteGroupMessageDeliveryAttempts(empty).status==MessageMutationStatus::kInvalidArgument,"empty_batch_rejected");
    auto duplicate=std::vector<GroupDeliveryAttemptCompletion>{fenced[0],fenced[0]};
    Check(f.repo.CompleteGroupMessageDeliveryAttempts(duplicate).status==MessageMutationStatus::kInvalidArgument,"duplicate_pair_rejected_before_SQL");
    auto bad=fenced[0];bad.message_id=0;
    Check(f.repo.CompleteGroupMessageDeliveryAttempts({bad}).status==MessageMutationStatus::kInvalidArgument,"zero_identity_rejected");
    bad=fenced[0];bad.lease_token="";
    Check(f.repo.CompleteGroupMessageDeliveryAttempts({bad}).status==MessageMutationStatus::kInvalidArgument,"empty_token_rejected");
    bad=fenced[0];bad.next_status=GroupDeliveryStatus::kDelivered;
    Check(f.repo.CompleteGroupMessageDeliveryAttempts({bad}).status==MessageMutationStatus::kInvalidArgument,"cannot_complete_into_delivered");
    bad=fenced[0];bad.retry_after_ms=600001;
    Check(f.repo.CompleteGroupMessageDeliveryAttempts({bad}).status==MessageMutationStatus::kInvalidArgument,"original_retry_bound");
    bool long_rejected=true;
    for(int i=0;i<3;++i) {bad=fenced[0];(i==0?bad.lease_token:i==1?bad.gateway_id:bad.error_code)=std::string(129,'x');
        long_rejected=long_rejected&&f.repo.CompleteGroupMessageDeliveryAttempts({bad}).status==MessageMutationStatus::kInvalidArgument;}
    Check(long_rejected&&updates==0&&pings==0,"all_invalid_entries_rejected_before_connection");
    f.Message(9203,{10002,10003});auto fault=f.Claim(9203);query_fault=1;auto pre=f.repo.CompleteGroupMessageDeliveryAttempts(fault);
    Check(query_fault==0&&pre.status==MessageMutationStatus::kStorageError&&f.Count(9203,"lease_token IS NOT NULL")==2,"SQL_error_keeps_original_leases");
    f.Message(9204,{10002,10003});fault=f.Claim(9204);
    f.Exec("CREATE TRIGGER own_complete_fault BEFORE UPDATE ON im_group_message_deliveries FOR EACH ROW BEGIN "
           "IF NEW.message_id=9204 AND NEW.recipient_user_id=10003 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='OWN_COMPLETION_FAULT'; END IF; END");
    auto trigger=f.repo.CompleteGroupMessageDeliveryAttempts(fault);
    Check(trigger.status==MessageMutationStatus::kStorageError&&f.Count(9204,"lease_token IS NOT NULL")==2,"one_statement_midway_fault_atomic_rollback");
    f.Message(9205,{10002,10003});fault=f.Claim(9205);query_fault=2;auto uncertain=f.repo.CompleteGroupMessageDeliveryAttempts(fault);
    Check(query_fault==0&&uncertain.status==MessageMutationStatus::kStorageError&&f.Count(9205,"lease_token IS NULL")==2,"testwrapper_committed_statement_fake_error_not_network_fault");
    auto repeated=f.repo.CompleteGroupMessageDeliveryAttempts(fault);
    Check(repeated.Succeeded()&&repeated.affected_rows==0,"uncertain_repeat_token_fence_noop");
    f.Message(9007199254740993ULL,{18446744073709551614ULL});auto high=f.Claim(9007199254740993ULL);
    auto exact=f.repo.CompleteGroupMessageDeliveryAttempts(high);
    Check(exact.Succeeded()&&exact.affected_rows==1&&
          f.Count(9007199254740993ULL,"recipient_user_id=18446744073709551614 AND lease_token IS NULL")==1,"uint64_exact_beyond_double_and_signed");
    f.Message(9206,{10002},2);
    auto deferred=f.repo.ClaimGroupMessageDeliveriesForRecipient(10002,"own","off-token",1,5000);
    Check(deferred.Succeeded()&&deferred.records.size()==1,"offline_claim_fixture");
    auto offline=rows[0];offline.message_id=9206;offline.lease_token="off-token";
    auto nochange=f.repo.CompleteGroupMessageDeliveryAttempts({offline});
    Check(nochange.Succeeded()&&nochange.affected_rows==0&&f.Count(9206,"delivery_status=2 AND lease_token='off-token'")==1,"original_pending_only_fence_preserves_deferred_rows");
    std::vector<std::uint64_t> large;
    for(std::uint64_t uid=10002;uid<=10258;++uid)large.push_back(uid);
    f.Message(9207,large);auto maximum=f.Claim(9207);
    Check(maximum.size()==256,"original_claim_maximum_fixture");
    auto oversized=maximum;oversized.push_back({9207,10258,"unclaimed",GroupDeliveryStatus::kPending,"",0,""});
    updates=pings=0;auto refused=f.repo.CompleteGroupMessageDeliveryAttempts(oversized);
    Check(refused.status==MessageMutationStatus::kInvalidArgument&&updates==0&&pings==0,"257_batch_rejected_without_SQL");
    auto maxresult=f.repo.CompleteGroupMessageDeliveryAttempts(maximum);
    Check(maxresult.Succeeded()&&maxresult.affected_rows==256&&f.Count(9207,"lease_token IS NULL")==257,"256_exact_bound_no_unclaimed_change");
    bool races=true;
    for(std::uint64_t i=0;i<20;++i) {
        auto mid=9300+i;f.Message(mid,{10002});auto one=f.Claim(mid);
        std::barrier start(2);GroupDeliveryMutationResult c,a;
        std::thread completer([&]{start.arrive_and_wait();c=f.repo.CompleteGroupMessageDeliveryAttempts(one);});
        std::thread acknowledger([&]{start.arrive_and_wait();a=f.repo.ConfirmGroupMessageDelivery(mid,10002);});
        completer.join();acknowledger.join();
        races=races&&c.Succeeded()&&a.Succeeded()&&f.Count(mid,"delivery_status=3 AND lease_token IS NULL")==1;
    }
    Check(races,"20_actual_concurrent_ACK_completion_never_revert");
    nlohmann::json timing=nlohmann::json::array();bool matched=true;
    std::vector<std::uint64_t> sixtyfour(large.begin(),large.begin()+64);
    for(std::uint64_t i=0;i<8;++i) {
        auto mid=9400+i;f.Message(mid,sixtyfour);auto work=f.Claim(mid,64);updates=pings=0;auto start=std::chrono::steady_clock::now();
        auto x=f.Complete(work,batch);auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
        int u=updates,p=pings;matched=matched&&x.Succeeded()&&x.affected_rows==64&&u==(batch?1:64)&&p==(batch?1:64)&&f.Count(mid,"lease_token IS NULL")==64;
        timing.push_back({{"sample",i},{"total_us",us},{"completion_commands",u},{"health_pings",p},{"affected_rows",x.affected_rows}});
    }
    Check(matched,"8_real_64row_completions_exact_durable_state_and_commands");
    std::cout<<nlohmann::json({{"status","GROUP_COMPLETION_REAL_MYSQL_PASS"},{"batch",batch},{"pool",db.pool_size},{"checks",checks},{"timing",timing},{"runtime_wiring",false},{"performance_acceptance",false}}).dump()<<'\n';
    f.pool.Shutdown();return 0;
}catch(const std::exception& error){std::cerr<<"[FAIL] "<<error.what()<<'\n';return 1;}
}
