#pragma once
// Pinned v2 protocol groundwork for an opt-in SDK batch ACK extension.
// This is not wired into the installed SDK or TinyIMX transport yet.
#include "apache/rocketmq/v2/service.pb.h"
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>

#if defined(__GNUC__)
#pragma GCC visibility push(hidden)
#endif
namespace tinyimx_rocketmq_batch {
namespace proto=apache::rocketmq::v2;
constexpr std::size_t kMaximumEntries=256;
struct AckResults {
    bool valid{false};
    std::vector<proto::Code> codes;
    std::string reason;
};
inline bool BoundedText(const std::string& value,std::size_t maximum) {
    return !value.empty()&&value.size()<=maximum&&value.find('\0')==std::string::npos;
}
inline bool ValidateRequest(const proto::AckMessageRequest& request) {
    if(request.entries_size()<1||request.entries_size()>static_cast<int>(kMaximumEntries)||
       !BoundedText(request.topic().name(),128)||!BoundedText(request.group().name(),128))return false;
    std::set<std::string> identities;
    for(const auto& entry:request.entries()) {
        if(!BoundedText(entry.message_id(),256)||!BoundedText(entry.receipt_handle(),8192)||
           !identities.insert(entry.message_id()).second)return false;
        // Lite topics use an additional routing contract; no silent batching.
        if(entry.has_lite_topic())return false;
    }
    return true;
}
inline AckResults Reject(std::size_t count,const char*reason) {
    AckResults result;
    if(count<=kMaximumEntries)result.codes.assign(count,proto::Code::INTERNAL_SERVER_ERROR);
    result.reason=reason;return result;
}
inline AckResults Decode(const proto::AckMessageRequest& request,const proto::AckMessageResponse& response) {
    const std::size_t count=static_cast<std::size_t>(request.entries_size());
    if(!ValidateRequest(request))return Reject(count,"invalid bounded batch request");
    if(!response.has_status()||static_cast<int>(response.status().code())==0)return Reject(count,"missing RPC status");
    const auto top=response.status().code();
    // A permission/authentication/whole-RPC error cannot be converted to
    // success by a contradictory entry. All inputs retain an error result.
    if(top!=proto::Code::OK&&top!=proto::Code::MULTIPLE_RESULTS) {
        AckResults result;result.valid=true;result.codes.assign(count,top);return result;
    }
    if(response.entries_size()!=request.entries_size())return Reject(count,"reply entry count differs");
    std::map<std::string,std::size_t> positions;
    for(std::size_t i=0;i<count;++i)positions.emplace(request.entries(static_cast<int>(i)).message_id(),i);
    std::vector<bool> seen(count,false);
    AckResults result;result.codes.assign(count,proto::Code::INTERNAL_SERVER_ERROR);
    std::set<proto::Code> distinct;
    for(const auto& entry:response.entries()) {
        auto found=positions.find(entry.message_id());
        if(found==positions.end()||seen[found->second])return Reject(count,"foreign or duplicate reply identity");
        if(!entry.has_status()||static_cast<int>(entry.status().code())==0||
           !BoundedText(entry.receipt_handle(),8192))return Reject(count,"missing bounded entry status/receipt");
        // The fixed proxy may replace an old receipt with a renewed handle.
        // Unique message_id is the correlation key; equal receipt text is
        // not required. The server still validates group/topic/receipt.
        seen[found->second]=true;result.codes[found->second]=entry.status().code();
        distinct.insert(entry.status().code());
    }
    if((top==proto::Code::OK&&(distinct.size()!=1||*distinct.begin()!=proto::Code::OK))||
       (top==proto::Code::MULTIPLE_RESULTS&&distinct.size()<2))return Reject(count,"RPC and entry statuses disagree");
    result.valid=true;return result;
}
}  // namespace tinyimx_rocketmq_batch
#if defined(__GNUC__)
#pragma GCC visibility pop
#endif
