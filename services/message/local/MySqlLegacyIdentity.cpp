#include "services/message/local/MySqlLegacyIdentity.h"
#include "services/message/local/NativePersistencePhaseTrace.h"
#include "common/db/MySqlConnectionPool.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <unordered_set>
namespace tinyimx::message::local {
namespace {
std::string BytesHex(const std::string& bytes) {
    constexpr char digits[]="0123456789ABCDEF";std::string out;out.reserve(bytes.size()*2);
    for(unsigned char c:bytes){out+=digits[c>>4];out+=digits[c&15];}return out;
}
}
class MySqlLegacyIdentity::ImmutableCache {
public:
    static constexpr std::size_t capacity=100000;
    // Deliberately restricted: spaces, Unicode and all other punctuation keep
    // the real SQL path. In particular, no local Unicode casefold/PAD emulation.
    static constexpr const char* alphabet="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_-:";
    std::array<std::string,128> weights;
    mutable std::shared_mutex mutex;
    std::unordered_set<std::uint64_t> positive_users;
    std::atomic<std::uint64_t> hits{0},sql_checks{0};
    bool Known(std::uint64_t from,std::uint64_t to)const {
        if(!to)return true; // Legacy CID lookup never checked user existence.
        std::shared_lock lock(mutex);
        return positive_users.contains(from)&&positive_users.contains(to);
    }
    void Remember(std::uint64_t from,std::uint64_t to){
        std::unique_lock lock(mutex);
        for(auto id:{from,to})if(positive_users.size()<capacity)positive_users.insert(id);
    }
    bool Canonical(const std::string& cid,std::string& key)const {
        for(unsigned char c:cid)if(c>=weights.size()||weights[c].empty())return false;
        key.clear();key.reserve(cid.size()*4);
        for(unsigned char c:cid)key+=weights[c];
        return true;
    }
};
MySqlLegacyIdentity::MySqlLegacyIdentity(MySqlConnectionPool* pool):MySqlLegacyIdentity(pool,false){}
MySqlLegacyIdentity::MySqlLegacyIdentity(MySqlConnectionPool* pool,bool enabled):pool_(pool) {
    if(!pool_)throw std::invalid_argument("legacy identity requires SQL authority");
    auto lease=pool_->Acquire();MySqlQueryResult rows;
    const std::string query="SELECT @@version,HEX(WEIGHT_STRING(_utf8mb4' ' COLLATE utf8mb4_unicode_ci)),"
        "(SELECT COLLATION_NAME FROM information_schema.columns WHERE table_schema=DATABASE()AND table_name='im_private_messages'AND column_name='client_message_id'),"
        "(SELECT PAD_ATTRIBUTE FROM information_schema.collations WHERE COLLATION_NAME='utf8mb4_unicode_ci')";
    if(!lease||!lease->Query(query,&rows)||rows.rows.size()!=1||rows.rows[0]!=std::vector<std::string>{"8.0.40","0209","utf8mb4_unicode_ci","PAD SPACE"})
        throw std::runtime_error("legacy identity version/collation authority mismatch");
    policy_="mysql-8.0.40-unicode-ci-padspace-v1";
    if(enabled){
        auto candidate=std::make_unique<ImmutableCache>();std::string sql;
        for(const unsigned char c:std::string(ImmutableCache::alphabet)){
            if(!sql.empty())sql+=" UNION ALL ";
            sql+="SELECT "+std::to_string(c)+",HEX(WEIGHT_STRING(CONVERT(0x"+BytesHex(std::string(1,char(c)))+
                " USING utf8mb4) COLLATE utf8mb4_unicode_ci))";
        }
        if(!lease->Query(sql,&rows)||rows.rows.size()!=std::string(ImmutableCache::alphabet).size())
            throw std::runtime_error("SQL-derived ASCII identity weights unavailable");
        for(const auto& row:rows.rows){
            if(row.size()!=2||row[0].empty()||!std::all_of(row[0].begin(),row[0].end(),[](unsigned char c){return c>='0'&&c<='9';}))
                throw std::runtime_error("invalid SQL ASCII identity weight row");
            const auto code=std::stoul(row[0]);
            if(code>=candidate->weights.size()||std::string(ImmutableCache::alphabet).find(char(code))==std::string::npos||
                !candidate->weights[code].empty()||row[1].size()!=4||row[1]=="0209"||
                !std::all_of(row[1].begin(),row[1].end(),[](unsigned char c){return(c>='0'&&c<='9')||(c>='A'&&c<='F');}))
                throw std::runtime_error("SQL ASCII identity weights violate fixed primary-weight policy");
            candidate->weights[code]=row[1];
        }
        cache_=std::move(candidate);
    }
}
MySqlLegacyIdentity::~MySqlLegacyIdentity()=default;
MySqlLegacyIdentity::CacheStats MySqlLegacyIdentity::ImmutableCacheStats()const {
    CacheStats out;if(!cache_)return out;
    out.hits=cache_->hits.load();out.sql_checks=cache_->sql_checks.load();
    std::shared_lock lock(cache_->mutex);out.positive_users=cache_->positive_users.size();return out;
}
LocalIdentityResult MySqlLegacyIdentity::Resolve(std::uint64_t from,std::uint64_t to,const std::string& cid) {
    using Status=MessageApplicationStatus;LocalIdentityResult out;
    if(!from||cid.empty()||cid.size()>64||(to&&from==to)){out.status=Status::kInvalidArgument;out.message="invalid legacy identity request";return out;}
    // Avoid replacement-character SELECT warnings accepting bytes that the
    // original strict utf8mb4 INSERT would reject. NUL itself is valid UTF-8.
    try{(void)nlohmann::json(cid).dump();}
    catch(const nlohmann::json::exception&){out.message="invalid legacy utf8mb4 CID bytes";return out;}
    if(cache_&&cache_->Known(from,to)&&cache_->Canonical(cid,out.canonical_cid)){
        ++cache_->hits;out.status=Status::kSucceeded;return out;
    }
    if(cache_)++cache_->sql_checks;
    auto lease=NativePersistencePhaseTrace::MeasureCurrent(NativePersistencePhaseTrace::IdentityAcquirePing,[&]{return pool_->Acquire();});if(!lease){out.message="legacy identity SQL authority unavailable";return out;}
    std::string query="SELECT HEX(WEIGHT_STRING(CONVERT(0x"+BytesHex(cid)+" USING utf8mb4) COLLATE utf8mb4_unicode_ci))";
    if(to)query+=",EXISTS(SELECT 1 FROM im_users WHERE user_id="+std::to_string(from)+"),EXISTS(SELECT 1 FROM im_users WHERE user_id="+std::to_string(to)+')';
    MySqlQueryResult rows;
    if(!NativePersistencePhaseTrace::MeasureCurrent(NativePersistencePhaseTrace::IdentityQuery,[&]{return lease->Query(query,&rows);})||rows.rows.size()!=1||rows.rows[0].size()!=(to?3:1)){out.message="legacy identity SQL query failed";return out;}
    const auto& row=rows.rows[0];std::string key=row[0];
    if(key.size()>1024||key.size()%4||!std::all_of(key.begin(),key.end(),[](unsigned char c){return(c>='0'&&c<='9')||(c>='A'&&c<='F');})){
        out.status=Status::kInvalidRecord;out.message="legacy identity authority returned invalid weights";return out;
    }
    // Strip collation SPACE weights, not input ASCII spaces: NBSP and Unicode
    // expansions/ignorables are governed by the real UCA4 comparator as well.
    while(key.ends_with("0209"))key.resize(key.size()-4);
    if(to&&(row[1]!="1"||row[2]!="1")){out.message="legacy logical user identity missing";return out;}
    if(cache_&&to)cache_->Remember(from,to);
    out.status=Status::kSucceeded;out.canonical_cid=std::move(key);return out;
}
}
