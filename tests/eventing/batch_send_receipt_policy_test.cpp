#include "services/eventing/rocketmq/BatchSendReceiptPolicy.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using P=tinyimx::eventing::rocketmq_transport::BatchSendReceiptPolicy;
namespace {
unsigned checks=0;
void Check(bool value,const char* text){if(!value)throw std::runtime_error(text);++checks;}
void NoConfirmation(const P::Result& value,P::Reason reason){
    Check(value.ConfirmedCount()==0,"ambiguous response advances no event");
    Check(value.reason==reason,"failure retains precise retry reason");
}
}
int main(){
 try{
    const std::vector<std::string> ids{"rpc-a","rpc-b","rpc-c"};
    auto r=P::Map(ids,true,P::Aggregate::kOk,{{"rpc-c",true},{"rpc-a",true},{"rpc-b",true}});
    Check(r.request_valid&&r.response_usable&&r.ConfirmedCount()==3,"all positive receipts correlate independently of order");
    r=P::Map(ids,true,P::Aggregate::kMultipleResults,{{"rpc-b",false},{"rpc-c",true},{"rpc-a",true}});
    Check(r.ConfirmedCount()==2&&r.entries[1]==P::Decision::kRetryable,"partial failure retries only unconfirmed event");
    r=P::Map(ids,true,P::Aggregate::kOk,{{"rpc-a",true},{"rpc-b",false}});
    Check(r.ConfirmedCount()==1&&r.entries[1]==P::Decision::kRetryable&&r.entries[2]==P::Decision::kRetryable,"aggregate OK never fills failed or missing receipt");
    r=P::Map(ids,true,P::Aggregate::kMultipleResults,{{"",false},{"rpc-b",true}});
    Check(r.ConfirmedCount()==1&&r.entries[1]==P::Decision::kConfirmed,"unidentified failure cannot confirm a missing entry");
    NoConfirmation(P::Map(ids,false,P::Aggregate::kOk,{{"rpc-a",true}}),P::Reason::kUncertainTransport);
    NoConfirmation(P::Map(ids,true,P::Aggregate::kOther,{{"rpc-a",true}}),P::Reason::kUnusableAggregate);
    NoConfirmation(P::Map(ids,true,P::Aggregate::kOk,{{"rpc-a",true},{"rpc-a",true}}),P::Reason::kMalformedReceipts);
    NoConfirmation(P::Map(ids,true,P::Aggregate::kOk,{{"rpc-a",true},{"rpc-a",false}}),P::Reason::kMalformedReceipts);
    NoConfirmation(P::Map(ids,true,P::Aggregate::kOk,{{"rpc-a",true},{"foreign",true}}),P::Reason::kMalformedReceipts);
    NoConfirmation(P::Map(ids,true,P::Aggregate::kOk,{{"rpc-a",true},{"",true}}),P::Reason::kMalformedReceipts);
    NoConfirmation(P::Map(ids,true,P::Aggregate::kOk,{{"rpc-a",true},{"rpc-b",true},{"rpc-c",true},{"",false}}),P::Reason::kMalformedReceipts);
    NoConfirmation(P::Map({},true,P::Aggregate::kOk,{}),P::Reason::kInvalidRequest);
    NoConfirmation(P::Map({"rpc-a","rpc-a"},true,P::Aggregate::kOk,{{"rpc-a",true}}),P::Reason::kInvalidRequest);
    NoConfirmation(P::Map({"rpc-a",""},true,P::Aggregate::kOk,{{"rpc-a",true}}),P::Reason::kInvalidRequest);
    NoConfirmation(P::Map({std::string(257,'x')},true,P::Aggregate::kOk,{}),P::Reason::kInvalidRequest);
    r=P::Map(ids,true,P::Aggregate::kOk,{});
    Check(r.ConfirmedCount()==0&&r.response_usable,"empty response leaves every request retryable");
    std::vector<std::string> boundary;std::vector<P::Entry> entries;
    for(unsigned i=0;i<P::max_entries;++i){boundary.push_back("rpc-"+std::to_string(i));entries.push_back({boundary.back(),true});}
    r=P::Map(boundary,true,P::Aggregate::kOk,entries);
    Check(r.count==32&&r.ConfirmedCount()==32,"exact32-entry bound is accepted");
    boundary.push_back("over-limit");
    NoConfirmation(P::Map(boundary,true,P::Aggregate::kOk,entries),P::Reason::kInvalidRequest);
    NoConfirmation(P::Map(ids,true,static_cast<P::Aggregate>(77),{{"rpc-a",true}}),P::Reason::kUnusableAggregate);
    r=P::MapPinnedNormalBatch(ids,true,P::Aggregate::kOk,{{"rpc-a,rpc-b,rpc-c",true}});
    Check(r.response_usable&&r.ConfirmedCount()==3,"pinned actual joined receipt confirms every exact unique ID");
    r=P::MapPinnedNormalBatch(ids,true,P::Aggregate::kOk,{{"rpc-c,rpc-a,rpc-b",true}});
    Check(r.ConfirmedCount()==3,"joined correlation does not infer request positions");
    Check(P::Map(ids,true,P::Aggregate::kOk,{{"rpc-a,rpc-b,rpc-c",true}}).ConfirmedCount()==0,"generic protocol does not silently accept pinned joined extension");
    NoConfirmation(P::MapPinnedNormalBatch(ids,true,P::Aggregate::kMultipleResults,{{"rpc-a,rpc-b,rpc-c",true}}),P::Reason::kMalformedReceipts);
    NoConfirmation(P::MapPinnedNormalBatch(ids,true,P::Aggregate::kOk,{{"rpc-a,rpc-b,rpc-c",false}}),P::Reason::kMalformedReceipts);
    NoConfirmation(P::MapPinnedNormalBatch(ids,true,P::Aggregate::kOk,{{"rpc-a,rpc-b",true}}),P::Reason::kMalformedReceipts);
    NoConfirmation(P::MapPinnedNormalBatch(ids,true,P::Aggregate::kOk,{{"rpc-a,rpc-b,rpc-b",true}}),P::Reason::kMalformedReceipts);
    NoConfirmation(P::MapPinnedNormalBatch(ids,true,P::Aggregate::kOk,{{"rpc-a,rpc-b,foreign",true}}),P::Reason::kMalformedReceipts);
    NoConfirmation(P::MapPinnedNormalBatch(ids,true,P::Aggregate::kOk,{{",rpc-a,rpc-b,rpc-c",true}}),P::Reason::kMalformedReceipts);
    NoConfirmation(P::MapPinnedNormalBatch(ids,true,P::Aggregate::kOk,{{"rpc-a,,rpc-b,rpc-c",true}}),P::Reason::kMalformedReceipts);
    NoConfirmation(P::MapPinnedNormalBatch(ids,true,P::Aggregate::kOk,{{"rpc-a,rpc-b,rpc-c,",true}}),P::Reason::kMalformedReceipts);
    NoConfirmation(P::MapPinnedNormalBatch(ids,true,P::Aggregate::kOk,{{"rpc-a,rpc-b,rpc-c",true},{"rpc-b",true}}),P::Reason::kMalformedReceipts);
    NoConfirmation(P::MapPinnedNormalBatch(ids,false,P::Aggregate::kOk,{{"rpc-a,rpc-b,rpc-c",true}}),P::Reason::kUncertainTransport);
    NoConfirmation(P::MapPinnedNormalBatch({"rpc-a,rpc-b","rpc-c"},true,P::Aggregate::kOk,{{"rpc-a,rpc-b,rpc-c",true}}),P::Reason::kInvalidRequest);
    NoConfirmation(P::MapPinnedNormalBatch(ids,true,static_cast<P::Aggregate>(77),{{"rpc-a,rpc-b,rpc-c",true}}),P::Reason::kUnusableAggregate);
    r=P::MapPinnedNormalBatch(ids,true,P::Aggregate::kMultipleResults,{{"rpc-a",true},{"rpc-b",false},{"rpc-c",true}});
    Check(r.ConfirmedCount()==2&&r.entries[1]==P::Decision::kRetryable,"per-entry partial failures remain independently mapped");
    boundary.pop_back();std::string joined;
    for(const auto& id:boundary){if(!joined.empty())joined+=',';joined+=id;}
    Check(P::MapPinnedNormalBatch(boundary,true,P::Aggregate::kOk,{{joined,true}}).ConfirmedCount()==32,"exact joined32-entry boundary accepted");
    NoConfirmation(P::MapPinnedNormalBatch(ids,true,P::Aggregate::kOk,{{std::string(8224,'x')+",rpc-a",true}}),P::Reason::kMalformedReceipts);
    std::cout<<"BATCH_SEND_RECEIPT_POLICY_PASS checks="<<checks<<" no_RPC_or_capacity_claim=1\n";return 0;
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
