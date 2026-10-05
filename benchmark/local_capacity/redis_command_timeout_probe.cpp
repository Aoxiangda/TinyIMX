#include "common/cache/RedisConnection.h"
#include "common/logging/Logger.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc,char**argv){
 try{
  if(argc!=4)throw std::runtime_error("OwnProbeArguments");const std::string mode=argv[1];const int port=std::stoi(argv[2]);
  const std::vector<std::string> modes{"ping","auth","select","eval"};bool allowed=false;for(const auto& m:modes)allowed|=mode==m;
  if(!allowed||port<1024||port>65535||port==6379||port==11434)throw std::runtime_error("OwnLoopbackEndpointOnly");
  namespace fs=std::filesystem;const fs::path output=argv[3];const fs::path original="/home/jackson7/projects/TinyIMX_publish/.local/codex/redis-command-timeout-original-probe-20261005";const fs::path fixed="/home/jackson7/projects/TinyIMX_publish/.local/codex/redis-command-timeout-fixed-probe-20261005";
  if((output.parent_path()!=original&&output.parent_path()!=fixed)||!fs::is_directory(output)||(output.filename()!=mode+"-healthy"&&output.filename()!=mode+"-stalled"))throw std::runtime_error("OwnCasePathRejected");
  tinyimx::LoggerConfig logs;logs.level="warn";logs.file="";if(!tinyimx::Logger::Instance().Init(logs))throw std::runtime_error("OwnLoggerInit");
  tinyimx::RedisConfig config;config.enable=true;config.host="127.0.0.1";config.port=port;config.db=mode=="select"?1:0;
  if(mode=="auth")config.password="public-owned-auth-fixture"; // Not any account credential.
  tinyimx::RedisConnection client;const auto start=std::chrono::steady_clock::now();const bool connected=client.Connect(config);bool success=connected;
  if(connected&&mode=="ping")success=client.Ping();
  if(connected&&mode=="eval"){auto value=client.EvalInteger("return 1",{},{});success=value&&*value==1;}
  const auto finish=std::chrono::steady_clock::now();const double seconds=std::chrono::duration<double>(finish-start).count();
  nlohmann::json result{{"status","OWN_SURROGATE_CASE_COMPLETE"},{"operation",mode},{"endpoint","own127loopback-ephemeral"},{"port",port},{"connect_succeeded",connected},{"operation_succeeded",success},{"client_connected_at_end",client.IsConnected()},{"elapsed_seconds",seconds},{"client_error",client.LastError()},{"installed_hiredis",std::to_string(HIREDIS_MAJOR)+"."+std::to_string(HIREDIS_MINOR)+"."+std::to_string(HIREDIS_PATCH)},{"real_redis_access",false},{"real_credentials",false}};
  const auto temp=output/"result.json.tmp";{std::ofstream stream(temp);stream<<result.dump(2)<<'\n';stream.flush();if(!stream)throw std::runtime_error("OwnEvidenceWrite");}fs::rename(temp,output/"result.json");return 0;
 }catch(const std::exception& error){std::cerr<<"OWN_TIMEOUT_PROBE_FAILURE="<<error.what()<<'\n';return 2;}
}
