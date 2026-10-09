// Fresh owned MySQL fixtures only; wrappers and faults never enter runtime ELF.
#include "common/db/MySqlConnectionPool.h"
#include "services/repository/MessageRepository.h"
#include <mysql/mysql.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string_view>
#include <thread>
namespace {
std::atomic<int> updates{0}, pings{0}, commits{0}, fault{0};
int checks=0;
void Check(bool yes,const char* name){if(!yes)throw std::runtime_error(name);++checks;std::cout<<"[PASS] "<<name<<'\n';}
}
extern "C" int __real_mysql_query(MYSQL*,const char*);
extern "C" int __real_mysql_ping(MYSQL*);
extern "C" int __real_mysql_commit(MYSQL*);
extern "C" int __wrap_mysql_ping(MYSQL* db){++pings;return __real_mysql_ping(db);}
extern "C" int __wrap_mysql_query(MYSQL* db,const char* text){
    const std::string_view sql(text);
    if(sql.starts_with("UPDATE im_group_message_deliveries SET delivery_status=3")){
        ++updates;int expected=1;
        if(fault.compare_exchange_strong(expected,0))
            return __real_mysql_query(db,"SELECT * FROM own_missing_confirm_table");
    }
    return __real_mysql_query(db,text);
}
extern "C" int __wrap_mysql_commit(MYSQL* db){
    ++commits;int expected=2;
    if(fault.compare_exchange_strong(expected,0)){
        const int result=__real_mysql_commit(db);
        return result==0?1:result; // Actual commit then synthetic API error; not a network fault.
    }
    return __real_mysql_commit(db);
}
namespace {
using namespace tinyimx;
using Id=std::pair<std::uint64_t,std::uint64_t>;
class Fixture {
public:
    MySqlConnectionPool pool;MessageRepository repo{&pool};
    void Exec(const std::string&sql){auto c=pool.Acquire();if(!c||!c->Execute(sql))throw std::runtime_error("OWN_FIXTURE_SQL_ERROR");}
    std::uint64_t Count(std::uint64_t mid,const std::string&filter){
        auto c=pool.Acquire();MySqlQueryResult q;
        if(!c||!c->Query("SELECT COUNT(*) FROM im_group_message_deliveries WHERE message_id="+
           std::to_string(mid)+" AND "+filter,&q))throw std::runtime_error("OWN_FIXTURE_READ_ERROR");
        return std::stoull(q.rows.at(0).at(0));
    }
    void Message(std::uint64_t mid,const std::vector<std::uint64_t>&users,int status=1){
        auto id=std::to_string(mid);
        Exec("INSERT INTO im_group_messages(message_id,client_message_id,group_id,from_user_id,message_type,"
             "content,membership_epoch,member_version,authorized_role) VALUES ("+id+
             ",'confirm-"+id+"',9001,10001,1,'owned bytes',1,1,1)");
        std::string rows;
        for(auto u:users){if(!rows.empty())rows+=",";rows+="("+id+","+std::to_string(u)+",9001,"+std::to_string(status)+")";}
        Exec("INSERT INTO im_group_message_deliveries(message_id,recipient_user_id,group_id,delivery_status) VALUES "+rows);
    }
};
bool AllSuccess(const std::vector<GroupDeliveryMutationResult>&v){return std::all_of(v.begin(),v.end(),[](const auto&r){return r.Succeeded();});}
bool AllFailure(const std::vector<GroupDeliveryMutationResult>&v){return std::all_of(v.begin(),v.end(),[](const auto&r){return r.status==MessageMutationStatus::kStorageError&&r.affected_rows==0;});}
}
int main(int argc,char**argv){
try{
    if(argc!=3)return 2;const bool coalesce=std::string_view(argv[2])=="1";
    std::ifstream file(argv[1]);nlohmann::json config;file>>config;auto j=config.at("mysql");
    MySqlConfig db;db.enable=true;db.host=j.at("host");db.port=j.at("port");db.database=j.at("database");
    db.user=j.at("user");db.password=j.at("password");db.pool_size=j.at("pool_size");
    if(!db.database.starts_with("codex_group_confirm_20261006_"))throw std::runtime_error("REFUSE_NONOWNED_SCHEMA");
    Fixture f;Check(f.pool.Initialize(db),"owned pool ready");
    std::string users;
    for(std::uint64_t u=10001;u<=10080;++u){if(!users.empty())users+=",";users+="("+std::to_string(u)+",'own-"+std::to_string(u)+"','Owned',1)";}
    users+=",(18446744073709551614,'own-high','Owned',1)";
    f.Exec("INSERT INTO im_users(user_id,username,nickname,status) VALUES "+users);
    f.Exec("INSERT INTO im_groups(group_id,name,owner_user_id,max_members) VALUES (9001,'Owned confirmation',10001,500)");
    f.Message(9501,{10002,10003,10004});
    f.Exec("UPDATE im_group_message_deliveries SET delivery_status=2,lease_owner='owned',lease_token='newer',"
           "lease_until=DATE_ADD(NOW(3),INTERVAL 5 SECOND),last_error_code='offline',last_gateway_id='exact',"
           "attempt_count=7,next_retry_at='2030-01-01 00:00:00.000',delivered_at='2020-01-01 00:00:00.000' "
           "WHERE message_id=9501 AND recipient_user_id=10003");
    f.Exec("UPDATE im_group_message_deliveries SET delivery_status=3,delivered_at='2021-01-01 00:00:00.000' "
           "WHERE message_id=9501 AND recipient_user_id=10004");
    updates=pings=commits=0;
    auto mixed=f.repo.ConfirmGroupMessageDeliveryBatch({{9501,10002},{9501,10002},{9501,10003},{9501,10004},{999999,10002}});
    const auto basic_u=updates.load(),basic_p=pings.load(),basic_c=commits.load();
    Check(AllSuccess(mixed)&&mixed.size()==5,"pending deferred duplicate delivered missing all succeed");
    Check(mixed[0].affected_rows==1&&mixed[1].affected_rows==0&&mixed[2].affected_rows==1&&mixed[3].affected_rows==0&&mixed[4].affected_rows==0,"per caller exact 1 0 1 0 0 affected rows");
    Check(basic_u==1&&basic_p==1&&basic_c==1,"one update one health ping one durable commit");
    Check(f.Count(9501,"delivery_status=3")==3,"all intended recipients durably confirmed");
    Check(f.Count(9501,"recipient_user_id=10003 AND lease_owner IS NULL AND lease_token IS NULL AND lease_until IS NULL AND last_error_code IS NULL AND last_gateway_id='exact' AND attempt_count=7 AND next_retry_at='2030-01-01 00:00:00.000' AND delivered_at='2020-01-01 00:00:00.000'")==1,"original metadata and coalesce timestamp retained");
    auto repeat=f.repo.ConfirmGroupMessageDeliveryBatch({{9501,10002},{9501,10003}});
    Check(AllSuccess(repeat)&&repeat[0].affected_rows==0&&repeat[1].affected_rows==0,"repeat confirmation successful per caller zero");
    updates=pings=commits=0;
    Check(f.repo.ConfirmGroupMessageDeliveryBatch({}).empty(),"empty batch rejected without SQL");
    auto invalid=f.repo.ConfirmGroupMessageDeliveryBatch({{0,10002},{9501,10003}});
    Check(invalid.size()==2&&invalid[0].status==MessageMutationStatus::kInvalidArgument&&invalid[1].status==MessageMutationStatus::kInvalidArgument,"mixed invalid identity rejects whole batch before SQL");
    auto too_many=f.repo.ConfirmGroupMessageDeliveryBatch(std::vector<Id>(65,{9501,10002}));
    Check(too_many.size()==65&&too_many[0].status==MessageMutationStatus::kInvalidArgument&&updates==0&&pings==0&&commits==0,"65 request bound rejected before SQL");
    Check(f.repo.ConfirmGroupMessageDelivery(0,10002).status==MessageMutationStatus::kInvalidArgument&&pings==0,"scalar invalid keeps original fast failure");
    f.Message(9502,{10002,10003});fault=1;updates=pings=commits=0;
    auto pre=f.repo.ConfirmGroupMessageDeliveryBatch({{9502,10002},{9502,10003}});
    const auto pre_u=updates.load();
    Check(AllFailure(pre)&&pre_u==1&&fault==0&&f.Count(9502,"delivery_status=1")==2,"statement error rolls back all locked rows without replay");
    f.Message(9503,{10002,10003});
    f.Exec("CREATE TRIGGER own_confirm_fault BEFORE UPDATE ON im_group_message_deliveries FOR EACH ROW BEGIN "
           "IF NEW.message_id=9503 AND NEW.recipient_user_id=10003 THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='OWN_CONFIRM_FAULT'; END IF; END");
    auto trigger=f.repo.ConfirmGroupMessageDeliveryBatch({{9503,10002},{9503,10003}});
    Check(AllFailure(trigger)&&f.Count(9503,"delivery_status=1")==2,"mid statement trigger error atomically rolls back first row");
    f.Message(9504,{10002,10003});fault=2;updates=pings=commits=0;
    auto uncertain=f.repo.ConfirmGroupMessageDeliveryBatch({{9504,10002},{9504,10003}});
    const auto uncertain_u=updates.load(),uncertain_c=commits.load();
    Check(AllFailure(uncertain)&&fault==0&&uncertain_u==1&&uncertain_c==1&&f.Count(9504,"delivery_status=3")==2,"actual commit synthetic error propagated to both callers no replay");
    auto recovered=f.repo.ConfirmGroupMessageDeliveryBatch({{9504,10002},{9504,10003}});
    Check(AllSuccess(recovered)&&recovered[0].affected_rows==0&&recovered[1].affected_rows==0,"explicit subsequent original confirmation observes uncertain committed zero");
    f.Message(9007199254740993ULL,{18446744073709551614ULL,10002});
    auto high=f.repo.ConfirmGroupMessageDeliveryBatch({{9007199254740993ULL,18446744073709551614ULL},{9007199254740993ULL,10002}});
    Check(AllSuccess(high)&&high[0].affected_rows==1&&high[1].affected_rows==1&&f.Count(9007199254740993ULL,"delivery_status=3")==2,"exact uint64 above double and signed range");
    std::vector<std::uint64_t> large;for(std::uint64_t u=10002;u<10066;++u)large.push_back(u);
    f.Message(9505,large);std::vector<Id> maximum;for(auto u:large)maximum.push_back({9505,u});
    updates=pings=commits=0;auto maxresult=f.repo.ConfirmGroupMessageDeliveryBatch(maximum);
    Check(AllSuccess(maxresult)&&std::all_of(maxresult.begin(),maxresult.end(),[](const auto&r){return r.affected_rows==1;})&&updates==1&&commits==1&&f.Count(9505,"delivery_status=3")==64,"64 requests one durable commit exact per caller state");
    // This statement is exactly the lock query shape used by the candidate.
    auto conn=f.pool.Acquire();MySqlQueryResult explain;
    Check(conn&&conn->Query("EXPLAIN SELECT message_id,recipient_user_id,delivery_status FROM im_group_message_deliveries WHERE (message_id,recipient_user_id) IN ((9505,10002),(9505,10003)) ORDER BY message_id,recipient_user_id FOR UPDATE",&explain),"lock query plan captured");
    nlohmann::json plan=explain.rows;bool indexed=false;
    for(const auto&row:explain.rows)for(const auto&value:row)if(value=="PRIMARY")indexed=true;
    Check(indexed,"bounded confirmation lookup uses primary key");conn.Reset();
    nlohmann::json timing=nlohmann::json::array();
    for(std::uint64_t round=0;round<6;++round){
        auto mid=9600+round;std::vector<std::uint64_t> recipients(large.begin(),large.begin()+32);f.Message(mid,recipients);
        std::barrier start(32);std::vector<GroupDeliveryMutationResult> results(32);std::vector<std::thread> threads;
        updates=pings=commits=0;auto began=std::chrono::steady_clock::now();
        for(std::size_t i=0;i<32;++i)threads.emplace_back([&,i]{start.arrive_and_wait();results[i]=f.repo.ConfirmGroupMessageDelivery(mid,recipients[i]);});
        for(auto&thread:threads)thread.join();
        const auto u=updates.load(),p=pings.load(),c=commits.load();
        auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-began).count();
        Check(AllSuccess(results)&&std::all_of(results.begin(),results.end(),[](const auto&r){return r.affected_rows==1;})&&f.Count(mid,"delivery_status=3")==32,"32 simultaneous scalar RPC contract confirmations exact");
        Check(coalesce?(u<32&&c<32&&c>0):(u==32&&c==0),"strict coalesce flag controls physical SQL commit grouping");
        timing.push_back({{"sample",round},{"total_us",us},{"updates",u},{"pings",p},{"explicit_commits",c}});
    }
    f.Message(9700,{10002});std::barrier start(32);
    std::vector<GroupDeliveryMutationResult> dup(32);std::vector<std::thread> threads;
    for(int i=0;i<32;++i)threads.emplace_back([&,i]{start.arrive_and_wait();dup[i]=f.repo.ConfirmGroupMessageDelivery(9700,10002);});
    for(auto&thread:threads)thread.join();
    std::uint64_t sum=0;for(const auto&r:dup)sum+=r.affected_rows;
    Check(AllSuccess(dup)&&sum==1&&f.Count(9700,"delivery_status=3")==1,"32 concurrent duplicate callers total affected exactly one");
    bool races=true;
    for(std::uint64_t i=0;i<12;++i){
        auto mid=9800+i;f.Message(mid,{10002,10003});
        auto claimed=f.repo.ClaimGroupMessageDeliveries("owned","owned-token",2,5000,mid);
        Check(claimed.Succeeded()&&claimed.records.size()==2,"race fixture claimed both recipients");
        std::vector<GroupDeliveryAttemptCompletion> entries;
        for(const auto&row:claimed.records)entries.push_back({mid,row.delivery.recipient_user_id,row.delivery.lease_token,GroupDeliveryStatus::kPending,"own",3000,""});
        std::barrier go(2);std::vector<GroupDeliveryMutationResult> ack;GroupDeliveryMutationResult complete;
        std::thread a([&]{go.arrive_and_wait();ack=f.repo.ConfirmGroupMessageDeliveryBatch({{mid,10002},{mid,10003}});});
        std::thread c([&]{go.arrive_and_wait();complete=f.repo.CompleteGroupMessageDeliveryAttempts(entries);});
        a.join();c.join();
        races=races&&AllSuccess(ack)&&complete.Succeeded()&&f.Count(mid,"delivery_status=3 AND lease_token IS NULL")==2;
    }
    Check(races,"12 actual batch ACK completion races never revert delivered");
    f.pool.Shutdown();
    Check(f.repo.ConfirmGroupMessageDelivery(999999,10002).status==MessageMutationStatus::kStorageError,"shutdown scalar fails instead of success or detached work");
    std::cout<<nlohmann::json({{"status","GROUP_CONFIRM_COALESCE_REAL_MYSQL_PASS"},{"coalesce",coalesce},{"pool",db.pool_size},{"checks",checks},{"timing",timing},{"lock_explain_rows",plan},{"runtime_deploy",false},{"performance_acceptance",false}}).dump()<<'\n';return 0;
}catch(const std::exception&e){std::cerr<<"[FAIL] "<<e.what()<<'\n';return 1;}
}
