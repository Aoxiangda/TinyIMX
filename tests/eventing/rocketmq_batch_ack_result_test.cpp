#include "BatchAckResult.h"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
namespace b=tinyimx_rocketmq_batch;
namespace p=apache::rocketmq::v2;
namespace {
unsigned checks=0;
void Check(bool ok,const char*name){if(!ok)throw std::runtime_error(name);++checks;}
p::AckMessageRequest Request(unsigned count=2){
 p::AckMessageRequest r;r.mutable_group()->set_name("owned-group");r.mutable_topic()->set_name("owned-topic");
 for(unsigned i=0;i<count;++i){auto*e=r.add_entries();e->set_message_id("id-"+std::to_string(i));e->set_receipt_handle("old-"+std::to_string(i));}return r;
}
p::AckMessageResponse Reply(const p::AckMessageRequest&r,p::Code top=p::Code::OK){
 p::AckMessageResponse v;v.mutable_status()->set_code(top);
 for(const auto&e:r.entries()){auto*x=v.add_entries();x->set_message_id(e.message_id());x->set_receipt_handle(e.receipt_handle());x->mutable_status()->set_code(p::Code::OK);}return v;
}
void NoSuccess(const b::AckResults&r,const char*why){Check(!r.valid&&std::none_of(r.codes.begin(),r.codes.end(),[](p::Code c){return c==p::Code::OK;}),why);}
}
int main(){try{
 auto r=Request();auto v=Reply(r);auto out=b::Decode(r,v);Check(out.valid&&out.codes==std::vector<p::Code>(2,p::Code::OK),"all OK");
 v.mutable_entries()->SwapElements(0,1);Check(b::Decode(r,v).valid,"reorder OK");
 v.mutable_status()->set_code(p::Code::MULTIPLE_RESULTS);v.mutable_entries(0)->mutable_status()->set_code(p::Code::INVALID_RECEIPT_HANDLE);
 out=b::Decode(r,v);Check(out.valid&&out.codes[0]==p::Code::OK&&out.codes[1]==p::Code::INVALID_RECEIPT_HANDLE,"partial status maps to original input position");
 v.mutable_entries(1)->set_receipt_handle("renewed-canonical-token");Check(b::Decode(r,v).valid,"canonical receipt rotation permitted");
 auto bad=v;bad.mutable_entries()->RemoveLast();NoSuccess(b::Decode(r,bad),"missing reply entry");
 bad=v;bad.mutable_entries(0)->set_message_id(bad.entries(1).message_id());NoSuccess(b::Decode(r,bad),"duplicate reply identity");
 bad=v;bad.mutable_entries(0)->set_message_id("foreign");NoSuccess(b::Decode(r,bad),"foreign reply identity");
 bad=v;bad.mutable_entries(0)->clear_status();NoSuccess(b::Decode(r,bad),"missing entry status");
 bad=v;bad.mutable_entries(0)->mutable_status()->set_code(static_cast<p::Code>(0));NoSuccess(b::Decode(r,bad),"unspecified entry status");
 bad=v;bad.mutable_entries(0)->clear_receipt_handle();NoSuccess(b::Decode(r,bad),"missing receipt");
 bad=v;bad.mutable_entries(0)->set_receipt_handle(std::string(8193,'r'));NoSuccess(b::Decode(r,bad),"oversize reply receipt");
 bad=v;bad.clear_status();NoSuccess(b::Decode(r,bad),"missing RPC status");
 bad=v;bad.mutable_status()->set_code(static_cast<p::Code>(0));NoSuccess(b::Decode(r,bad),"unspecified RPC status");
 bad=v;bad.mutable_status()->set_code(p::Code::OK);NoSuccess(b::Decode(r,bad),"OK aggregate cannot hide failed entry");
 bad=Reply(r,p::Code::MULTIPLE_RESULTS);NoSuccess(b::Decode(r,bad),"mixed aggregate cannot mean all OK");
 bad=Reply(r,p::Code::MULTIPLE_RESULTS);for(auto&e:*bad.mutable_entries())e.mutable_status()->set_code(p::Code::INVALID_RECEIPT_HANDLE);NoSuccess(b::Decode(r,bad),"mixed aggregate cannot mean one uniform error");
 bad.mutable_entries(1)->mutable_status()->set_code(p::Code::FORBIDDEN);out=b::Decode(r,bad);Check(out.valid&&out.codes[0]!=p::Code::OK&&out.codes[1]!=p::Code::OK,"two distinct failures remain failures");
 bad.clear_entries();bad.mutable_status()->set_code(p::Code::UNAUTHORIZED);out=b::Decode(r,bad);Check(out.valid&&out.codes==std::vector<p::Code>(2,p::Code::UNAUTHORIZED),"whole RPC unauthorized");
 bad=Reply(r,p::Code::UNAUTHORIZED);out=b::Decode(r,bad);Check(out.valid&&out.codes==std::vector<p::Code>(2,p::Code::UNAUTHORIZED),"unauthorized cannot be overridden by success entries");
 auto invalid=Request(0);Check(!b::ValidateRequest(invalid),"empty request");
 invalid=Request(257);NoSuccess(b::Decode(invalid,v),"overlarge request bounded error allocation");Check(b::Decode(invalid,v).codes.empty(),"overlarge invalid request does not allocate N errors");
 invalid=r;invalid.mutable_entries(0)->clear_message_id();Check(!b::ValidateRequest(invalid),"empty input identity");
 invalid=r;invalid.mutable_entries(0)->clear_receipt_handle();Check(!b::ValidateRequest(invalid),"empty input receipt");
 invalid=r;invalid.mutable_entries(0)->set_message_id(invalid.entries(1).message_id());Check(!b::ValidateRequest(invalid),"duplicate input identities with different receipts refused");
 invalid=r;invalid.mutable_entries(0)->set_message_id(std::string(257,'i'));Check(!b::ValidateRequest(invalid),"identity length bound");
 invalid=r;invalid.mutable_entries(0)->set_receipt_handle(std::string(8193,'r'));Check(!b::ValidateRequest(invalid),"input receipt length bound");
 invalid=r;invalid.mutable_entries(0)->set_receipt_handle(std::string("a\0b",3));Check(!b::ValidateRequest(invalid),"NUL receipt refused");
 invalid=r;invalid.mutable_entries(0)->set_lite_topic("lite");Check(!b::ValidateRequest(invalid),"lite routing not silently batched");
 invalid=r;invalid.mutable_topic()->clear_name();Check(!b::ValidateRequest(invalid),"missing topic");
 invalid=r;invalid.mutable_group()->clear_name();Check(!b::ValidateRequest(invalid),"missing group");
 invalid=r;invalid.mutable_topic()->set_name(std::string(129,'t'));Check(!b::ValidateRequest(invalid),"topic length bound");
 invalid=r;invalid.mutable_group()->set_name(std::string(129,'g'));Check(!b::ValidateRequest(invalid),"group length bound");
 bad=Reply(r);bad.add_entries()->CopyFrom(bad.entries(0));NoSuccess(b::Decode(r,bad),"surplus response entry");
 const unsigned named_checks=checks;
 std::mt19937 random(218);unsigned fuzz=0;
 for(unsigned iteration=0;iteration<500;++iteration){
  const unsigned n=2+random()%255;auto req=Request(n);auto response=Reply(req,p::Code::MULTIPLE_RESULTS);
  std::vector<p::Code> expected(n,p::Code::OK);expected[0]=p::Code::INVALID_RECEIPT_HANDLE;
  for(unsigned i=0;i<n;++i){response.mutable_entries(i)->mutable_status()->set_code(expected[i]);response.mutable_entries(i)->set_receipt_handle("rotated-"+std::to_string(i));}
  for(unsigned i=n;i>1;--i)response.mutable_entries()->SwapElements(i-1,random()%i);
  const auto request_wire=req.SerializeAsString(),response_wire=response.SerializeAsString();
  p::AckMessageRequest parsed_req;p::AckMessageResponse parsed_resp;Check(parsed_req.ParseFromString(request_wire)&&parsed_resp.ParseFromString(response_wire),"actual protobuf wire parse");
  const auto got=b::Decode(parsed_req,parsed_resp);Check(got.valid&&got.codes==expected,"random reply order preserves all input outcomes");
  Check(parsed_req.SerializeAsString()==request_wire&&parsed_resp.SerializeAsString()==response_wire,"decoder never mutates wire objects");++fuzz;
 }
 std::cout<<"BATCH_ACK_RESULT_PASS named="<<named_checks<<" permutation_rounds="<<fuzz<<" checks="<<checks<<std::endl;return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
