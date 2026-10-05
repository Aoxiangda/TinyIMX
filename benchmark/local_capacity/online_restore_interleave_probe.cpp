#include "services/cache/OnlineStatusCache.h"
#include "common/logging/Logger.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace restore_probe {
using Json=nlohmann::json;
namespace fs=std::filesystem;
constexpr char Prefix[]="codex:online-restore-interleave-probe-20261005:";
Json reports=Json::array();fs::path output;
void save(const std::string& filename,const Json& value){const auto temp=output/(filename+".tmp");{std::ofstream f(temp);f<<value.dump(2)<<'\n';f.flush();if(!f)throw std::runtime_error("OwnEvidenceWrite");}fs::rename(temp,output/filename);}
void require(bool pass,const char* message){if(!pass)throw std::runtime_error(message);}
Json state(const tinyimx::GetOnlineStatusResult& result){Json j{{"status",static_cast<int>(result.status)},{"found",result.Found()}};if(result.Found()){const auto& r=*result.record;j["record"]={{"user_id",r.user_id},{"gateway_id",r.gateway_id},{"connection_name",r.connection_name},{"login_time",r.login_time}};}return j;}
bool owner(const tinyimx::GetOnlineStatusResult& r,const std::string& gateway,const std::string& connection){return r.Found()&&r.record->gateway_id==gateway&&r.record->connection_name==connection;}
void run(tinyimx::RedisConnectionPool& pool){
 tinyimx::OnlineStatusCache cache(&pool,Prefix);
 for(std::uint64_t id=60001;id<=60004;++id){auto lease=pool.Acquire();require(bool(lease),"OwnPreflightLease");auto exists=lease->EvalInteger("return redis.call('EXISTS',KEYS[1])",{std::string(Prefix)+std::to_string(id)},{});require(exists&&*exists==0,"OwnFixtureCollisionPreserved");}
 // This is a deterministic public-cache-API reconstruction of the source
 // window. The local-session predicate is modeled explicitly; no live
 // Gateway/TCP session, login, or disconnected client is manipulated.
 for(int kind=0;kind<4;++kind){const std::uint64_t id=60001+kind;const auto missing=cache.RefreshOnlineIfMatch(id,"probe-local","old-connection",120);require(missing.status==tinyimx::RefreshOnlineIfMatchStatus::kNotFound,"OwnExpectedMissing");
  Json sequence=Json::array({"owner-checked refresh:NotFound","modeled local current check:true"});Json before,after;bool expected_red=false,safety=true;
  if(kind==0){require(cache.SetOnline(id,"probe-local","old-connection",120).Succeeded(),"OwnNormalRestore");after=state(cache.GetOnlineStatus(id));require(owner(cache.GetOnlineStatus(id),"probe-local","old-connection"),"OwnNormalControlOwner");sequence.push_back("original unconditional SetOnline(local,old):Stored");}
  if(kind==1){require(cache.SetOnline(id,"probe-remote","new-connection",300).Succeeded(),"OwnRemoteFixture");before=state(cache.GetOnlineStatus(id));require(owner(cache.GetOnlineStatus(id),"probe-remote","new-connection"),"OwnRemoteBefore");sequence.push_back("competing remote owner SetOnline(remote,new):Stored");require(cache.SetOnline(id,"probe-local","old-connection",120).Succeeded(),"OwnLegacyRestore");after=state(cache.GetOnlineStatus(id));expected_red=true;safety=owner(cache.GetOnlineStatus(id),"probe-remote","new-connection");require(!safety&&owner(cache.GetOnlineStatus(id),"probe-local","old-connection"),"ExpectedLegacyOwnerOverwriteNotReproduced");sequence.push_back("original unconditional SetOnline(local,old):Stored overwrites remote");}
  if(kind==2){sequence.push_back("modeled local session unbind:current=false");const auto cleanup=cache.SetOfflineIfMatch(id,"probe-local","old-connection");require(cleanup.status==tinyimx::SetOfflineIfMatchStatus::kNotFound,"OwnNegativeOfflineCleanup");sequence.push_back("own original offline cleanup:NotFound (no deletion)");before=state(cache.GetOnlineStatus(id));require(!cache.GetOnlineStatus(id).Found(),"OwnOfflineBeforeRestore");require(cache.SetOnline(id,"probe-local","old-connection",120).Succeeded(),"OwnLegacyDisconnectedRestore");after=state(cache.GetOnlineStatus(id));expected_red=true;safety=!cache.GetOnlineStatus(id).Found();require(!safety&&owner(cache.GetOnlineStatus(id),"probe-local","old-connection"),"ExpectedLegacyDisconnectedRestoreNotReproduced");sequence.push_back("original unconditional SetOnline(local,old):Stored creates record after cleanup");}
  if(kind==3){require(cache.SetOnline(id,"probe-local","old-connection",120).Succeeded(),"OwnReverseLegacyRestore");before=state(cache.GetOnlineStatus(id));require(cache.SetOnline(id,"probe-remote","new-connection",300).Succeeded(),"OwnReverseRemoteSet");after=state(cache.GetOnlineStatus(id));require(owner(cache.GetOnlineStatus(id),"probe-remote","new-connection"),"OwnReverseControlOwner");sequence.push_back("original restore before competing remote:SetOnline(remote,new), final owner correct");}
  const char* names[]={"normal-missing-restore-control","remote-owner-set-between-check-and-restore","unbind-cleanup-between-check-and-restore","remote-set-after-restore-control"};reports.push_back({{"case",names[kind]},{"user_id",id},{"sequence",sequence},{"before_restore_or_takeover",before},{"after",after},{"expected_original_red",expected_red},{"desired_safety_invariant_pass",safety},{"local_session_predicate","Explicit deterministic model, not live Gateway/TCP session proof"}});save("cases.json",reports);
 }
 save("result.json",{{"status","ORIGINAL_ONLINE_RESTORE_INTERLEAVING_WINDOWS_REPRODUCED"},{"healthy_controls",2},{"original_red_windows",2},{"cases",reports},{"namespace",Prefix},{"new_owned_keys",4},{"production_keys",false},{"live_gateway_sessions_tested",false},{"limits","Actual cache API operation interleaving with explicit modeled local-current predicate. Two source control-flow windows demonstrated, no frequency or live Gateway race/capacity claim. Missing-only restore plus lifecycle ordering/fencing need separate production implementation and tests."}});
}
}
int main(int argc,char**argv){namespace p=restore_probe;try{if(argc!=4)throw std::runtime_error("OwnArguments");p::output=argv[3];if(p::output!="/home/jackson7/projects/TinyIMX_publish/.local/codex/online-restore-interleave-original-20261005"||!p::fs::is_directory(p::output)||std::string(argv[2])!="172.18.0.13")throw std::runtime_error("OwnStageEndpointRejected");tinyimx::LoggerConfig logging;logging.level="warn";logging.file="";p::require(tinyimx::Logger::Instance().Init(logging),"OwnLoggerInit");std::ifstream file(p::fs::path(argv[1])/"gateway-a.json");p::require(bool(file),"OwnConfigFile");p::Json j;file>>j;j=j.at("redis");tinyimx::RedisConfig config;config.enable=j.at("enable").get<bool>();config.host=argv[2];config.port=j.at("port").get<int>();config.db=j.at("db").get<int>();config.password=j.at("password").get<std::string>();config.pool_size=1;tinyimx::RedisConnectionPool pool;p::require(pool.Initialize(config),"OwnPoolInitialize");p::run(pool);return 0;}catch(const std::exception& error){if(!p::output.empty()&&p::fs::is_directory(p::output))p::save("failed.json",{{"status","FAIL"},{"message",error.what()},{"completed_cases",p::reports.size()}});std::cerr<<"OWN_RESTORE_PROBE_FAILURE="<<error.what()<<'\n';return 2;}}
