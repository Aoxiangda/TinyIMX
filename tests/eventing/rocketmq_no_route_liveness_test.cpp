// Calls the actual SDK implementation function, with its actual empty route
// cache. No copied method body, fake SDK, mock callback dispatcher or network.
// The explicit-instantiation accessor is test-only; SDK headers/ABI are intact.
#include "SimpleConsumerImpl.h"
#include "rocketmq/ErrorCode.h"
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

using namespace ROCKETMQ_NAMESPACE;
struct AssignmentCompletionTag {
  typedef void (SimpleConsumerImpl::*type)(const std::string&,std::function<void(const std::error_code&)>);
  friend type access(AssignmentCompletionTag);
};
template<class Tag,typename Tag::type Method>struct TestMemberAccess {
  friend typename Tag::type access(Tag){return Method;}
};
template struct TestMemberAccess<AssignmentCompletionTag,&SimpleConsumerImpl::refreshAssignment>;
static void Require(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
int main(int argc,char**argv){
 try{
  Require(argc==3,"owned result directory and baseline/candidate mode required");
  const std::string root=argv[1],mode=argv[2],prefix="/run/user/1000/tinyimx-sdk-no-route-r1695-";
  Require(root.find(prefix)==0&&root.find('/',prefix.size())==std::string::npos&&root.find("..",prefix.size())==std::string::npos,"new owned no-route namespace only");
  Require(mode=="baseline"||mode=="candidate","explicit qualification mode");
  auto consumer=std::make_shared<SimpleConsumerImpl>("codex_perf1695_no_route");
  unsigned completions=0;std::error_code terminal;
  const auto begin=std::chrono::steady_clock::now();
  (consumer.get()->*access(AssignmentCompletionTag{}))("codex_perf1695_no_route",[&](const std::error_code&ec){++completions;terminal=ec;});
  const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
  Require(elapsed<1000,"empty route completion must not wait for network work");
  if(mode=="baseline")Require(completions==0,"the exact old missing-callback branch must be reproduced");
  else Require(completions==1&&terminal==ErrorCode::NotFound,"actual missing route must complete exactly once with failure");
  consumer.reset();
  const std::string status=mode=="baseline"?"ACTUAL_OLD_EMPTY_ROUTE_CALLBACK_OMISSION_REPRODUCED":"ACTUAL_SDK_EMPTY_ROUTE_ERROR_CALLBACK_ONCE_PASS";
  std::ofstream out(root+"/"+mode+"-callback-result.json");
  out<<"{\"status\":\""<<status<<"\",\"callback_count\":"<<completions<<",\"error_value\":"<<terminal.value()<<",\"elapsed_ms\":"<<elapsed<<",\"network_requests\":0,\"actual_SDK_method\":true,\"whole_startup_or_IM_claim\":false}\n";
  out.close();Require(!out.fail(),"closed result write");std::cout<<status<<std::endl;return 0;
 }catch(const std::exception&error){std::cerr<<"ACTUAL_CALLBACK_COMPLETION_FAILURE "<<error.what()<<std::endl;return 1;}
}
