// Real MySQL, fresh owned schema only. Wrappers are linked only in this test.
// They count SQL and simulate exact failure returns; service image has none.
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
thread_local int updates=0;
thread_local bool lease_update=false,affected_fault=false;
thread_local int commit_fault=0;
int passed=0;
void Check(bool yes,const char* name) {
    if(!yes)throw std::runtime_error(name);
    ++passed;std::cout<<"[PASS] "<<name<<'\n';
}
}
extern "C" int __real_mysql_query(MYSQL*,const char*);
extern "C" my_ulonglong __real_mysql_affected_rows(MYSQL*);
extern "C" int __real_mysql_commit(MYSQL*);
extern "C" int __wrap_mysql_query(MYSQL* db,const char* sql) {
    lease_update=std::string_view(sql).starts_with("UPDATE im_group_message_deliveries SET lease_owner=");
    if(lease_update)++updates;
    return __real_mysql_query(db,sql);
}
extern "C" my_ulonglong __wrap_mysql_affected_rows(MYSQL* db) {
    auto count=__real_mysql_affected_rows(db);
    if(lease_update&&affected_fault){affected_fault=false;return 0;}
    return count;
}
extern "C" int __wrap_mysql_commit(MYSQL* db) {
    int mode=commit_fault;commit_fault=0;
    if(mode==1)return 1;
    int code=__real_mysql_commit(db);
    return mode==2&&code==0?1:code;
}
namespace {
using namespace tinyimx;
class Fixture {
public:
    MySqlConnectionPool pool;
    MessageRepository repo{&pool};
    void Exec(const std::string& sql) {
        auto c=pool.Acquire();if(!c||!c->Execute(sql))throw std::runtime_error("OWN_FIXTURE_SQL_ERROR");
    }
    std::uint64_t Count(std::uint64_t mid,const std::string& filter) {
        auto c=pool.Acquire();MySqlQueryResult q;
        if(!c||!c->Query("SELECT COUNT(*) FROM im_group_message_deliveries WHERE message_id="+
            std::to_string(mid)+" AND "+filter,&q))throw std::runtime_error("OWN_FIXTURE_READ_ERROR");
        return std::stoull(q.rows.at(0).at(0));
    }
    void Message(std::uint64_t mid,const std::vector<std::uint64_t>& users,int status=1,std::uint64_t group=9001) {
        auto id=std::to_string(mid);
        Exec("INSERT INTO im_group_messages(message_id,client_message_id,group_id,from_user_id,message_type,"
             "content,membership_epoch,member_version,authorized_role) VALUES ("+id+
             ",'claim-"+id+"',9001,10001,1,'exact own bytes',1,1,1)");
        std::string rows;
        for(auto uid:users){if(!rows.empty())rows+=",";rows+="("+id+","+std::to_string(uid)+","+
            std::to_string(group)+","+std::to_string(status)+")";}
        if(!rows.empty())Exec("INSERT INTO im_group_message_deliveries("
            "message_id,recipient_user_id,group_id,delivery_status) VALUES "+rows);
    }
};
}
int main(int argc,char** argv) {
try {
    if(argc!=3)throw std::runtime_error("CONFIG_AND_EXPECTED_FLAG_REQUIRED");
    bool enabled=std::string(argv[2])=="1";std::ifstream stream(argv[1]);
    nlohmann::json cfg;stream>>cfg;auto j=cfg.at("mysql");
    MySqlConfig db;db.enable=true;db.host=j.at("host");db.port=j.at("port");
    db.database=j.at("database");db.user=j.at("user");db.password=j.at("password");db.pool_size=j.at("pool_size");
    if(!db.database.starts_with("codex_group_claim_20261006_"))throw std::runtime_error("REFUSE_NONOWNED_SCHEMA");
    Fixture f;Check(f.pool.Initialize(db),"owned_pool_ready");
    std::string users;
    for(std::uint64_t uid=10001;uid<=10270;++uid){
        if(!users.empty())users+=",";
        users+="("+std::to_string(uid)+",'claim-user-"+std::to_string(uid)+"','Owned',1)";
    }
    users+=",(18446744073709551614,'claim-high','Owned high',1)";
    f.Exec("INSERT INTO im_users(user_id,username,nickname,status) VALUES "+users);
    f.Exec("INSERT INTO im_groups(group_id,name,owner_user_id,max_members) VALUES "
           "(9001,'Owned claims',10001,500),(9002,'Owned invalid group',10001,500)");
    const std::string owner="own'quoted\\owner",token="token'quoted\\bytes";
    f.Message(9101,{10002,10003,10004});updates=0;
    auto claim=f.repo.ClaimGroupMessageDeliveries(owner,token,2,5000,9101);
    Check(claim.Succeeded()&&claim.records.size()==2&&claim.records[0].delivery.recipient_user_id==10002&&
          claim.records[1].delivery.recipient_user_id==10003,"online_sorted_limit_exact");
    Check(updates==(enabled?1:2),"online_actual_update_count");
    Check(f.Count(9101,"attempt_count=1 AND lease_until>NOW(3)")==2&&
          f.Count(9101,"attempt_count=0 AND lease_token IS NULL")==1,"only_selected_rows_durable");
    auto stored=f.repo.FindGroupMessageDelivery(9101,10002);
    Check(stored.Found()&&stored.record.delivery.lease_owner==owner&&stored.record.delivery.lease_token==token&&
          claim.records[0].delivery.attempt_count==1,"escaped_bytes_and_returned_attempt");
    auto wrong=f.repo.CompleteGroupMessageDeliveryAttempt(9101,10002,"wrong",GroupDeliveryStatus::kPending,"own",0,"");
    Check(wrong.Succeeded()&&wrong.affected_rows==0,"wrong_token_cannot_release");
    auto ack=f.repo.ConfirmGroupMessageDelivery(9101,10002);
    auto old=f.repo.CompleteGroupMessageDeliveryAttempt(9101,10002,token,GroupDeliveryStatus::kPending,"own",0,"");
    Check(ack.Succeeded()&&ack.affected_rows==1&&old.Succeeded()&&old.affected_rows==0&&
          f.Count(9101,"delivery_status=3 AND lease_token IS NULL")==1,"ack_winner_not_reverted");
    auto remain=f.repo.ClaimGroupMessageDeliveries("other","other",2,5000,9101);
    Check(remain.Succeeded()&&remain.records.size()==1&&remain.records[0].delivery.recipient_user_id==10004,
          "live_lease_excluded");
    updates=0;auto empty=f.repo.ClaimGroupMessageDeliveries("empty","empty",2,5000,9101);
    Check(empty.Succeeded()&&empty.records.empty()&&updates==0,"empty_claim_no_update");
    Check(f.repo.ClaimGroupMessageDeliveries("",token,2,5000,9101).status==MessageQueryStatus::kInvalidArgument&&
          f.repo.ClaimGroupMessageDeliveries(owner,token,257,5000,9101).status==MessageQueryStatus::kInvalidArgument&&
          f.repo.ClaimGroupMessageDeliveries(owner,token,2,99,9101).status==MessageQueryStatus::kInvalidArgument,
          "original_argument_bounds");
    f.Message(9102,{10002,10003},2);f.Message(9103,{10002},2);updates=0;
    auto off=f.repo.ClaimGroupMessageDeliveriesForRecipient(10002,owner,token,2,5000);
    Check(off.Succeeded()&&off.records.size()==2&&off.records[0].message.message_id==9102&&
          off.records[1].message.message_id==9103&&
          off.records[0].delivery.delivery_status==GroupDeliveryStatus::kDeferredOffline,"offline_exact_order_status");
    Check(updates==(enabled?1:2),"offline_actual_update_count");
    Check(f.Count(9102,"recipient_user_id=10003 AND attempt_count=0 AND lease_token IS NULL")==1,
          "offline_other_recipient_untouched");
    f.Message(9104,{10011,10012});
    f.Exec("CREATE TRIGGER own_claim_fault BEFORE UPDATE ON im_group_message_deliveries FOR EACH ROW BEGIN "
           "IF NEW.message_id=9104 AND NEW.recipient_user_id=10012 THEN SIGNAL SQLSTATE '45000' "
           "SET MESSAGE_TEXT='OWN_CLAIM_FAULT'; END IF; END");
    auto fault=f.repo.ClaimGroupMessageDeliveries(owner,token,2,5000,9104);
    Check(fault.status==MessageQueryStatus::kStorageError&&fault.records.empty(),"sql_fault_no_work");
    Check(f.Count(9104,"attempt_count=0 AND lease_token IS NULL")==2,"sql_fault_whole_rollback");
    f.Message(9105,{10002,10003});affected_fault=true;
    auto mismatch=f.repo.ClaimGroupMessageDeliveries(owner,token,2,5000,9105);
    Check(!affected_fault&&mismatch.status==MessageQueryStatus::kStorageError&&mismatch.records.empty()&&
          f.Count(9105,"attempt_count=0 AND lease_token IS NULL")==2,"affected_mismatch_rollback");
    f.Message(9106,{10002,10003});commit_fault=1;
    auto pre=f.repo.ClaimGroupMessageDeliveries(owner,token,2,5000,9106);
    Check(commit_fault==0&&pre.status==MessageQueryStatus::kStorageError&&pre.records.empty()&&
          f.Count(9106,"attempt_count=0 AND lease_token IS NULL")==2,"precommit_error_rollback_no_work");
    f.Message(9107,{10002,10003});commit_fault=2;
    auto lost=f.repo.ClaimGroupMessageDeliveries(owner,token,2,100,9107);
    Check(commit_fault==0&&lost.status==MessageQueryStatus::kStorageError&&lost.records.empty()&&
          f.Count(9107,"attempt_count=1 AND lease_token IS NOT NULL")==2,"simulated_postcommit_error_durable_no_work");
    std::this_thread::sleep_for(std::chrono::milliseconds(140));
    auto recovered=f.repo.ClaimGroupMessageDeliveries("recover","new-token",2,5000,9107);
    Check(recovered.Succeeded()&&recovered.records.size()==2&&
          f.Count(9107,"attempt_count=2 AND lease_token='new-token'")==2,"uncertain_commit_original_lease_recovery");
    f.Message(9108,{10002},1,9002);
    auto bad=f.repo.ClaimGroupMessageDeliveries(owner,token,2,5000,9108);
    Check(bad.status==MessageQueryStatus::kInvalidRecord&&bad.records.empty()&&
          f.Count(9108,"attempt_count=0 AND lease_token IS NULL")==1,"typed_group_identity_rejection");
    f.Message(9007199254740993ULL,{18446744073709551614ULL});
    auto high=f.repo.ClaimGroupMessageDeliveries(owner,token,1,5000,9007199254740993ULL);
    Check(high.Succeeded()&&high.records.size()==1&&
          high.records[0].delivery.message_id==9007199254740993ULL&&
          high.records[0].delivery.recipient_user_id==18446744073709551614ULL&&
          f.Count(9007199254740993ULL,"recipient_user_id=18446744073709551614 AND attempt_count=1")==1,
          "uint64_exact_beyond_double_and_signed_range");
    std::vector<std::uint64_t> large;
    for(std::uint64_t uid=10002;uid<=10258;++uid)large.push_back(uid);
    f.Message(9109,large);updates=0;
    auto max=f.repo.ClaimGroupMessageDeliveries(owner,token,256,5000,9109);
    Check(max.Succeeded()&&max.records.size()==256&&updates==(enabled?1:256)&&
          f.Count(9109,"attempt_count=1")==256&&f.Count(9109,"attempt_count=0")==1,"maximum256_exact_bound");
    large.resize(64);f.Message(9110,large);
    std::barrier start(2);ListGroupDeliveryWorkResult a,b;
    std::thread one([&]{start.arrive_and_wait();a=f.repo.ClaimGroupMessageDeliveries("one","one",32,5000,9110);});
    std::thread two([&]{start.arrive_and_wait();b=f.repo.ClaimGroupMessageDeliveries("two","two",32,5000,9110);});
    one.join();two.join();Check(a.Succeeded()&&b.Succeeded(),"actual_concurrent_claims_succeeded");
    std::set<std::uint64_t> unique;bool no_duplicates=true;
    for(auto* q:{&a,&b})for(const auto& row:q->records)
        no_duplicates=unique.insert(row.delivery.recipient_user_id).second&&no_duplicates;
    // Joined SKIP LOCKED permits fewer results under a concurrent row lock.
    auto tail=f.repo.ClaimGroupMessageDeliveries("tail","tail",64,5000,9110);
    Check(tail.Succeeded(),"concurrent_tail_success");
    for(const auto& row:tail.records)
        no_duplicates=unique.insert(row.delivery.recipient_user_id).second&&no_duplicates;
    Check(no_duplicates&&unique.size()==64&&f.Count(9110,"attempt_count=1")==64,"concurrent_exact_union_once");
    if(db.pool_size>1) {
        f.Message(9111,{10002,10003});auto lock=f.pool.Acquire();MySqlQueryResult q;
        Check(lock&&lock->BeginTransaction()&&lock->Query(
            "SELECT message_id FROM im_group_message_deliveries WHERE message_id=9111 AND recipient_user_id=10002 FOR UPDATE",&q),
            "owned_explicit_lock_ready");
        auto skip=f.repo.ClaimGroupMessageDeliveries(owner,token,2,5000,9111);
        Check(skip.Succeeded()&&skip.records.size()==1&&skip.records[0].delivery.recipient_user_id==10003,
              "original_skip_locked_preserved");
        Check(lock->Rollback(),"owned_explicit_lock_released");
    }
    nlohmann::json timing=nlohmann::json::array();
    for(std::uint64_t i=0;i<8;++i) {
        auto mid=9200+i;f.Message(mid,large);updates=0;auto before=std::chrono::steady_clock::now();
        auto measured=f.repo.ClaimGroupMessageDeliveries(owner,token,64,5000,mid);
        auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-before).count();
        int count=updates;
        Check(measured.Succeeded()&&measured.records.size()==64&&count==(enabled?1:64)&&
              f.Count(mid,"attempt_count=1")==64,"measured64_durable_and_sql_count");
        timing.push_back({{"claim_us",us},{"lease_update_commands",count},{"records",64}});
    }
    std::cout<<nlohmann::json({{"status","GROUP_DELIVERY_CLAIM_REAL_MYSQL_PASS"},{"enabled",enabled},
        {"pool",db.pool_size},{"checks",passed},{"timing",timing},{"performance_acceptance",false},
        {"postcommit_fault","link_wrapper_real_commit_then_simulated_error_not_network_failure"}}).dump()<<'\n';
    f.pool.Shutdown();return 0;
}catch(const std::exception& error){std::cerr<<"[FAIL] "<<error.what()<<'\n';return 1;}
}
