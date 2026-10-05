// Isolated durable network-roundtrip mechanism; fixed owned probe tables only.
// This does not implement production persistence/recovery or certify its errors.
// Every result consumed: https://dev.mysql.com/doc/c-api/8.0/en/c-api-multiple-queries.html
#include <mysql/mysql.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <sys/resource.h>
#include <time.h>

namespace {
using Json=nlohmann::json;
using Clock=std::chrono::steady_clock;
constexpr int kWorkers=16;
constexpr const char* kMessages="codex_sql_rtt_messages_20261005";
constexpr const char* kOutbox="codex_sql_rtt_outbox_20261005";
struct SqlResult { std::uint64_t affected{},insert_id{}; std::vector<std::vector<std::string>> rows; };
class Connection {
public:
    Connection(const Json& config,const std::string& host) {
        handle_=mysql_init(nullptr);if(!handle_)throw std::runtime_error("OWN_MYSQL_INIT_FAILED");
        unsigned timeout=5;
        mysql_options(handle_,MYSQL_OPT_CONNECT_TIMEOUT,&timeout);
        mysql_options(handle_,MYSQL_OPT_READ_TIMEOUT,&timeout);
        mysql_options(handle_,MYSQL_OPT_WRITE_TIMEOUT,&timeout);
        const auto user=config.at("user").get<std::string>(),password=config.at("password").get<std::string>(),database=config.at("database").get<std::string>();
        if(!mysql_real_connect(handle_,host.c_str(),user.c_str(),password.c_str(),database.c_str(),config.at("port").get<unsigned>(),nullptr,CLIENT_MULTI_STATEMENTS)) {
            const auto error=mysql_errno(handle_);mysql_close(handle_);handle_=nullptr;
            throw std::runtime_error("OWN_MYSQL_CONNECT_ERRNO_"+std::to_string(error));
        }
        if(mysql_set_character_set(handle_,"utf8mb4")) {mysql_close(handle_);handle_=nullptr;throw std::runtime_error("OWN_MYSQL_CHARSET_FAILED");}
        try {
            if(Run("SELECT 1").at(0).rows!=std::vector<std::vector<std::string>>{{"1"}})throw std::runtime_error("OWN_MYSQL_WARMUP_FAILED");
        } catch(...) {mysql_close(handle_);handle_=nullptr;throw;}
        calls=0;
    }
    ~Connection(){if(handle_)mysql_close(handle_);}
    Connection(const Connection&)=delete;
    Connection& operator=(const Connection&)=delete;
    void Ping(){++pings;if(mysql_ping(handle_))Fail("PING");}
    bool InTransaction() const {return (handle_->server_status & SERVER_STATUS_IN_TRANS)!=0;}
    void RollbackOwn() noexcept {if(handle_&&InTransaction())mysql_rollback(handle_);}
    std::vector<SqlResult> Run(const std::string& sql) {
        ++calls;if(mysql_real_query(handle_,sql.data(),sql.size()))Fail("QUERY");
        std::vector<SqlResult> results;
        for(;;) {
            SqlResult result;result.insert_id=mysql_insert_id(handle_);
            auto affected=mysql_affected_rows(handle_);result.affected=affected==static_cast<my_ulonglong>(-1)?0:affected;
            MYSQL_RES* raw=mysql_store_result(handle_);
            if(raw) {
                std::unique_ptr<MYSQL_RES,decltype(&mysql_free_result)> owned(raw,mysql_free_result);
                const auto fields=mysql_num_fields(raw);MYSQL_ROW values;
                while((values=mysql_fetch_row(raw))) {
                    const auto* lengths=mysql_fetch_lengths(raw);std::vector<std::string> row;
                    for(unsigned i=0;i<fields;++i)row.emplace_back(values[i]?std::string(values[i],lengths[i]):std::string{});
                    result.rows.push_back(std::move(row));
                    if(result.rows.size()>2)throw std::runtime_error("OWN_RESULT_BOUND");
                }
                if(mysql_errno(handle_))Fail("FETCH");
            } else if(mysql_field_count(handle_))Fail("STORE");
            results.push_back(std::move(result));
            const int more=mysql_next_result(handle_);
            if(more<0)break;
            if(more>0)Fail("NEXT_RESULT");
            if(results.size()>=8)throw std::runtime_error("OWN_STATEMENT_BOUND");
        }
        return results;
    }
    std::uint64_t calls{},pings{};
private:
    [[noreturn]] void Fail(const char* phase){throw std::runtime_error(std::string("OWN_MYSQL_")+phase+"_ERRNO_"+std::to_string(mysql_errno(handle_)));}
    MYSQL* handle_{};
};
struct Row {std::int64_t total{},ping{},precheck{},begin{},sql{},cpu{},late{};std::uint64_t mid{},sender{},recipient{};bool ok{};std::string error;};
std::int64_t Cpu(){timespec t{};if(clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t))return -1;return t.tv_sec*1000000000LL+t.tv_nsec;}
std::int64_t Elapsed(Clock::time_point start){return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-start).count();}
std::int64_t Us(timeval value){return value.tv_sec*1000000LL+value.tv_usec;}
Json Distribution(std::vector<std::int64_t> v){
    if(v.empty())return nullptr;std::sort(v.begin(),v.end());long double total=0;for(auto x:v)total+=x;
    auto p=[&](int n){return v[(v.size()*n+99)/100-1]/1000000.0;};
    return Json{{"samples",v.size()},{"mean_ms",static_cast<double>(total/v.size()/1000000)},{"p50_ms",p(50)},{"p95_ms",p(95)},{"p99_ms",p(99)},{"max_ms",v.back()/1000000.0}};
}
bool Token(const std::string& s){return !s.empty()&&s.size()<=20&&std::all_of(s.begin(),s.end(),[](char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');});}
}
int main(int argc,char** argv){
    try {
        if(argc!=8)return 2;const std::string mode=argv[1],run=argv[7];const int rate=std::stoi(argv[2]),seconds=std::stoi(argv[3]);
        if((mode!="single"&&mode!="batch")||!Token(run)||rate<1||rate>300||seconds<1||seconds>20)return 2;
        std::ifstream input(argv[5]);Json config;input>>config;const auto& db=config.at("mysql");if(!db.at("enable").get<bool>())return 3;
        std::vector<std::unique_ptr<Connection>> connections;
        for(int i=0;i<kWorkers;++i)connections.push_back(std::make_unique<Connection>(db,argv[4]));
        config.clear();const auto count=static_cast<std::size_t>(rate)*seconds;std::vector<Row> rows(count);
        std::atomic<std::size_t> next{0};std::atomic<int> failures{0};std::barrier ready(kWorkers+1);Clock::time_point origin;std::vector<std::thread> workers;
        for(int w=0;w<kWorkers;++w)workers.emplace_back([&,w]{
            mysql_thread_init();auto& connection=*connections[w];ready.arrive_and_wait();
            for(;;){const auto index=next.fetch_add(1);if(index>=count)break;auto& row=rows[index];
                const auto due=origin+std::chrono::nanoseconds(index*1000000000LL/rate);std::this_thread::sleep_until(due);
                const auto started=Clock::now();const auto cpu=Cpu();row.late=std::max<std::int64_t>(0,std::chrono::duration_cast<std::chrono::nanoseconds>(started-due).count());
                row.sender=700001+index%10000;row.recipient=700001+(index+1)%10000;const auto from=std::to_string(row.sender),to=std::to_string(row.recipient);const auto cid=run+"n"+std::to_string(index);const std::string content(128,'x');
                const auto predicate="from_user_id="+from+" AND client_message_id='"+cid+"'";
                try {
                    auto phase=Clock::now();connection.Ping();row.ping=Elapsed(phase);
                    phase=Clock::now();const auto prior=connection.Run(std::string("SELECT message_id FROM ")+kMessages+" WHERE "+predicate+" LIMIT 1");row.precheck=Elapsed(phase);
                    if(prior.size()!=1||!prior[0].rows.empty())throw std::runtime_error("OWN_PRECHECK_COLLISION");
                    phase=Clock::now();connection.Run("START TRANSACTION");row.begin=Elapsed(phase);if(!connection.InTransaction())throw std::runtime_error("OWN_TRANSACTION_NOT_STARTED");
                    const std::string insert=std::string("INSERT INTO ")+kMessages+" (client_message_id,from_user_id,to_user_id,message_type,content,delivery_status) VALUES ('"+cid+"',"+from+","+to+",1,'"+content+"',0)";
                    const std::string identity=std::string("SELECT message_id,client_message_id,from_user_id,to_user_id,message_type,content,delivery_status,created_at FROM ")+kMessages+" WHERE "+predicate+" LIMIT 1";
                    const std::string outbox=std::string("INSERT INTO ")+kOutbox+" (event_id,event_type,schema_version,aggregate_type,aggregate_id,producer_service,topic,tag,message_key,payload,status,attempt_count,next_attempt_at) SELECT CONCAT('codex.rtt:',message_id),'codex.rtt.v1',1,'private_message',CAST(message_id AS CHAR),'codex-component-probe','codex-component-probe','codex.rtt.v1',CONCAT('codex.rtt:',message_id),JSON_OBJECT('message_id',message_id,'from_user_id',from_user_id,'to_user_id',to_user_id),0,0,NOW(3) FROM "+kMessages+" WHERE "+predicate;
                    phase=Clock::now();std::vector<SqlResult> result;
                    if(mode=="single"){
                        for(const auto& statement:std::vector<std::string>{insert,identity,outbox,"COMMIT"}){auto part=connection.Run(statement);if(part.size()!=1)throw std::runtime_error("OWN_SINGLE_SHAPE");result.push_back(std::move(part[0]));}
                    } else result=connection.Run(insert+";"+identity+";"+outbox+";COMMIT");
                    row.sql=Elapsed(phase);
                    if(result.size()!=4||result[0].affected!=1||result[2].affected!=1||connection.InTransaction()||result[1].rows.size()!=1||result[1].rows[0].size()!=8)throw std::runtime_error("OWN_RESULT_OR_COMMIT_STATE");
                    const auto& record=result[1].rows[0];row.mid=std::stoull(record[0]);
                    row.ok=row.mid>0&&result[0].insert_id==row.mid&&record[1]==cid&&record[2]==from&&record[3]==to&&record[4]=="1"&&record[5]==content&&record[6]=="0"&&!record[7].empty();
                    if(!row.ok)throw std::runtime_error("OWN_COMMITTED_IDENTITY_MISMATCH");
                } catch(const std::exception& error){row.error=error.what();row.ok=false;++failures;connection.RollbackOwn();}
                row.total=Elapsed(started);const auto end_cpu=Cpu();row.cpu=cpu>=0&&end_cpu>=cpu?end_cpu-cpu:-1;
            }
            mysql_thread_end();
        });
        rusage before{},after{};getrusage(RUSAGE_SELF,&before);origin=Clock::now();ready.arrive_and_wait();for(auto& worker:workers)worker.join();const auto elapsed=std::chrono::duration<double>(Clock::now()-origin).count();getrusage(RUSAGE_SELF,&after);
        std::uint64_t calls=0,pings=0;for(auto& connection:connections){calls+=connection->calls;pings+=connection->pings;}
        std::vector<std::int64_t> total,ping,precheck,begin,sql,cpu,late;Json raw=Json::array();int valid=0;
        for(std::size_t index=0;index<count;++index){const auto& row=rows[index];valid+=row.ok;total.push_back(row.total);ping.push_back(row.ping);precheck.push_back(row.precheck);begin.push_back(row.begin);sql.push_back(row.sql);late.push_back(row.late);if(row.cpu>=0)cpu.push_back(row.cpu);raw.push_back({index,row.ok,row.mid,row.sender,row.recipient,row.total,row.ping,row.precheck,row.begin,row.sql,row.cpu,row.late,row.error});}
        const auto user=Us(after.ru_utime)-Us(before.ru_utime),system=Us(after.ru_stime)-Us(before.ru_stime);
        const bool complete=valid==static_cast<int>(count)&&failures==0&&pings==count&&calls==count*(mode=="single"?6:3);
        Json output{{"status",complete?"TRANSACTION_ROUNDTRIP_COMPONENT_COMPLETE":"FAIL"},{"mode",mode},{"run",run},{"planned",count},{"committed_identity_correct",valid},{"failures",failures.load()},{"rate_per_second",rate},{"nominal_seconds",seconds},{"workers",kWorkers},{"ping_calls",pings},{"query_calls",calls},{"native_requests_including_ping",calls+pings},{"mysql_client_version",mysql_get_client_info()},{"elapsed_seconds",elapsed},{"caller_wall",Distribution(total)},{"ping_wall",Distribution(ping)},{"precheck_wall",Distribution(precheck)},{"begin_wall",Distribution(begin)},{"four_statement_wall",Distribution(sql)},{"thread_cpu",Distribution(cpu)},{"scheduled_lateness",Distribution(late)},{"process_user_cpu_seconds",user/1000000.0},{"process_system_cpu_seconds",system/1000000.0},{"process_mean_cpu_cores",(user+system)/1000000.0/elapsed},{"voluntary_context_switches",after.ru_nvcsw-before.ru_nvcsw},{"involuntary_context_switches",after.ru_nivcsw-before.ru_nivcsw},{"raw_columns",{"index","correct","message_id","sender","recipient","caller_ns","ping_ns","precheck_ns","begin_ns","four_statement_ns","cpu_ns","scheduled_late_ns","error"}},{"raw",std::move(raw)},{"performance_acceptance",false},{"limits","Only own two durable copied tables, restored sender/recipient FKs andrealCOMMIT; same4SQLstatements onevs4calls. PING/precheck/BEGIN retained, driverflagalreadyproductON. Modelsnormaltransaction networkcost, outboxsyntheticJSON notproductionEventCodec. Does not certify idempotent races, forcederrors/tampering/uncertaincommit/recovery orreal10k RPC capacity. Native-to-Docker route differsproduction; processCPU calleronly, cgroupwholebackground separate."}};
        std::ofstream out(argv[6]);out<<output.dump()<<'\n';if(!out)return 6;
        return complete?0:1;
    } catch(const std::exception& error){std::cerr<<"OWN_PROBE_FAILURE="<<error.what()<<'\n';return 5;}
}
