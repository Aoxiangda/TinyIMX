#include "services/message/local/MySqlLegacyIdentity.h"
#include "common/config/Config.h"
#include "common/db/MySqlConnectionPool.h"
#include <algorithm>
#include <atomic>
#include <future>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
using namespace tinyimx;
using namespace tinyimx::message;
using namespace tinyimx::message::local;
namespace {
void Check(bool v,const char* why){if(!v)throw std::runtime_error(why);std::cout<<"PASS "<<why<<'\n';}
std::string Hex(const std::string& s){const char*d="0123456789ABCDEF";std::string out;for(unsigned char c:s){out+=d[c>>4];out+=d[c&15];}return out;}
std::string Trim(std::string s){while(s.ends_with("0209"))s.resize(s.size()-4);return s;}
void Compare(MySqlConnectionPool& pool,MySqlLegacyIdentity& cached,const std::vector<std::string>& ids,std::size_t& verified){
    for(std::size_t first=0;first<ids.size();first+=128){
        const auto end=std::min(first+128,ids.size());std::string query;
        for(std::size_t n=first;n<end;++n){if(n!=first)query+=" UNION ALL ";
            query+="SELECT "+std::to_string(n)+",HEX(WEIGHT_STRING(CONVERT(0x"+Hex(ids[n])+" USING utf8mb4) COLLATE utf8mb4_unicode_ci))";}
        auto lease=pool.Acquire();MySqlQueryResult rows;
        if(!lease||!lease->Query(query,&rows)||rows.rows.size()!=end-first)throw std::runtime_error("read-only real SQL oracle batch failed");
        std::vector<bool> seen(end-first);
        for(const auto& row:rows.rows){
            if(row.size()!=2)throw std::runtime_error("SQL oracle row shape");const auto n=std::stoull(row[0]);
            if(n<first||n>=end||seen[n-first])throw std::runtime_error("SQL oracle row identity");seen[n-first]=true;
            const auto got=cached.Resolve(500001,500002,ids[n]);
            if(got.status!=MessageApplicationStatus::kSucceeded||got.canonical_cid!=Trim(row[1]))throw std::runtime_error("cached canonical identity differs from MySQL");
            ++verified;
        }
    }
}
}
int main(int argc,char**argv){
    try{
        if(argc!=3)return 2;const std::string mode=argv[2];if(mode!="full"&&mode!="sanitizer")return 2;
        Config config;Check(config.LoadFromFile(argv[1])&&
            (config.MySql().database=="codex_perf041_20261018_r59101"||
             config.MySql().database=="codex_perf994_20261025_r1_release"||
             config.MySql().database=="codex_perf994_20261025_r2_release"),
            "exact retained or current owned schema, SELECT only");
        auto settings=config.MySql();settings.pool_size=4;MySqlConnectionPool pool;Check(pool.Initialize(settings),"bounded real SQL pool");
        MySqlLegacyIdentity original(&pool),cached(&pool,true);
        Check(original.Policy()==cached.Policy(),"same fixed MySQL collation/store identity policy");
        const auto a=original.Resolve(500001,500002,"seed"),b=cached.Resolve(500001,500002,"seed");
        Check(a.status==b.status&&a.status==MessageApplicationStatus::kSucceeded&&a.canonical_cid==b.canonical_cid,"cold request uses real SQL and same identity");
        auto stat=cached.ImmutableCacheStats();Check(stat.hits==0&&stat.sql_checks==1&&stat.positive_users==2,"only two SQL-verified immutable UIDs cached");
        const std::string alphabet="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_-:";
        std::vector<std::string> ids;
        for(char c:alphabet)ids.emplace_back(1,c);
        for(char x:alphabet)for(char y:alphabet)ids.push_back(std::string()+x+y);
        if(mode=="full")for(char x:alphabet)for(char y:alphabet)for(char z:alphabet)ids.push_back(std::string()+x+y+z);
        std::mt19937_64 random(0x20261022);
        for(int i=0;i<4096;++i){std::string id;const auto size=1+random()%64;for(std::size_t n=0;n<size;++n)id+=alphabet[random()%alphabet.size()];ids.push_back(std::move(id));}
        for(char c:alphabet)ids.emplace_back(64,c);
        std::size_t verified=0;Compare(pool,cached,ids,verified);
        stat=cached.ImmutableCacheStats();Check(stat.sql_checks==1&&stat.hits==ids.size(),"all restricted ASCII checks use local SQL-derived weights after cold identity check");
        std::cout<<"REAL_SQL_ASCII_EQUIVALENCE_CASES "<<verified<<'\n';
        const std::vector<std::string> fallback={"A ","A\t","A.","A/","Résumé","Straße","I\u0307","\u00a0","\u200d","\U0001f41d"};
        const auto before_fallback=cached.ImmutableCacheStats();Compare(pool,cached,fallback,verified);
        stat=cached.ImmutableCacheStats();Check(stat.sql_checks==before_fallback.sql_checks+fallback.size(),"spaces Unicode and other punctuation keep real SQL");
        for(const auto [from,to]:std::vector<std::pair<std::uint64_t,std::uint64_t>>{{999999999,500001},{500001,999999999}}){
            for(int retry=0;retry<2;++retry){const auto x=original.Resolve(from,to,"missing"),y=cached.Resolve(from,to,"missing");Check(x.status==y.status&&y.status!=MessageApplicationStatus::kSucceeded,"unknown identities never accepted by a positive-cache miss");}}
        Check(cached.ImmutableCacheStats().positive_users==2,"failed existence checks do not populate positive or negative cache");
        Check(original.Resolve(999999999,0,"lookup").canonical_cid==cached.Resolve(999999999,0,"lookup").canonical_cid,"legacy CID lookup without recipient retains no-existence-check semantics");
        for(const auto& id:std::vector<std::string>{"",std::string(65,'A'),std::string("bad\xff",4)}){
            const auto x=original.Resolve(500001,500002,id),y=cached.Resolve(500001,500002,id);Check(x.status==y.status&&y.status!=MessageApplicationStatus::kSucceeded,"invalid length or UTF8 rejected before cache");}
        const auto before_threads=cached.ImmutableCacheStats();std::vector<std::future<bool>> threads;
        for(int worker=0;worker<8;++worker)threads.push_back(std::async(std::launch::async,[&,worker]{for(int n=0;n<1000;++n){const auto id="thread_"+std::to_string(worker)+":"+std::to_string(n);if(cached.Resolve(500001,500002,id).status!=MessageApplicationStatus::kSucceeded)return false;}return true;}));
        for(auto& f:threads)Check(f.get(),"concurrent immutable positive lookup/weights stable");
        stat=cached.ImmutableCacheStats();Check(stat.hits==before_threads.hits+8000&&stat.sql_checks==before_threads.sql_checks,"parallel hot requests add no SQL checks");
        Check(original.ImmutableCacheStats().hits==0&&original.ImmutableCacheStats().sql_checks==0,"default disabled constructor has no cache telemetry/state");
        if(config.MySql().database=="codex_perf994_20261025_r1_release"||config.MySql().database=="codex_perf994_20261025_r2_release"){
            // Reconcile application-counted commands with the same server's
            // performance_schema digest observer before treating it as exact.
            std::atomic<unsigned> completed{0};threads.clear();
            for(unsigned worker=0;worker<8;++worker)threads.push_back(std::async(std::launch::async,[&]{
                for(unsigned n=0;n<1000;++n){
                    auto lease=pool.Acquire();MySqlQueryResult rows;
                    if(!lease||!lease->Query("SELECT user_id AS perf994_counter_probe FROM im_users WHERE user_id=500001",&rows)||
                       rows.rows!=std::vector<std::vector<std::string>>{{"500001"}})return false;
                    ++completed;
                }
                return true;
            }));
            bool all=true;for(auto& f:threads)all=f.get()&&all;
            Check(all&&completed==8000,"application counts exactly8000 real concurrent SQL observer-probe queries");
            std::cout<<"PERFORMANCE_SCHEMA_COUNTER_PROBE_EXPECTED "<<completed.load()<<'\n';
        }
        pool.Shutdown();
        Check(cached.Resolve(500001,500002,"known_after_pool_stop").status==MessageApplicationStatus::kSucceeded,"stable positive metadata remains readable without SQL; native admission is separate");
        Check(cached.Resolve(500001,999999999,"unknown_after_pool_stop").status!=MessageApplicationStatus::kSucceeded,"unknown user cannot bypass unavailable authority");
        Check(cached.Resolve(500001,500002,"Unicode后备").status!=MessageApplicationStatus::kSucceeded,"non-ASCII cannot bypass unavailable authority");
        Check(original.Resolve(500001,500002,"known_after_pool_stop").status!=MessageApplicationStatus::kSucceeded,"disabled mode retains original per-request SQL dependency");
        bool rejected=false;try{MySqlLegacyIdentity unavailable(&pool,true);}catch(const std::exception&){rejected=true;}Check(rejected,"cache startup requires live exact SQL authority");
        std::cout<<"PASS READONLY_IMMUTABLE_IDENTITY_CONTRACTS mode="<<mode<<" equivalent="<<verified<<" native_owner_fencing_and_formal_performance_pending=1\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
