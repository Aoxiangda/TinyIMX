#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace tinyimx { namespace eventing { namespace rocketmq_transport {

// The v2 protocol permits partial send failures. A successful batch RPC or
// aggregate OK never confirms every entry. Only a unique, explicitly positive
// receipt correlated to this request can advance that event's publication.
// This allocation-free policy is independent of the SDK/transport adapter.
class BatchSendReceiptPolicy final {
public:
    static constexpr std::size_t max_entries = 32;
    static constexpr std::size_t max_message_id_bytes = 256;
    enum class Aggregate { kOk, kMultipleResults, kOther };
    enum class Decision { kRetryable, kConfirmed };
    enum class Reason { kNone, kInvalidRequest, kUncertainTransport,
                        kUnusableAggregate, kMalformedReceipts };
    struct Entry {
        std::string message_id;
        bool positive{false};
        Entry() = default;
        Entry(std::string id, bool accepted)
            : message_id(std::move(id)), positive(accepted) {}
    };
    struct Result {
        std::array<Decision, max_entries> entries{};
        std::size_t count{0};
        bool request_valid{false};
        bool response_usable{false};
        Reason reason{Reason::kInvalidRequest};
        std::size_t ConfirmedCount() const noexcept {
            std::size_t n=0;
            for(std::size_t i=0;i<count;++i)n+=entries[i]==Decision::kConfirmed;
            return n;
        }
    };

    static Result Map(const std::vector<std::string>& expected_ids,
                      bool transport_completed,
                      Aggregate aggregate,
                      const std::vector<Entry>& receipts) noexcept {
        Result out;
        if(expected_ids.empty()||expected_ids.size()>max_entries)return out;
        out.count=expected_ids.size();
        for(std::size_t i=0;i<out.count;++i){
            if(expected_ids[i].empty()||expected_ids[i].size()>max_message_id_bytes)return out;
            for(std::size_t j=0;j<i;++j)
                if(expected_ids[i]==expected_ids[j])return out;
        }
        out.request_valid=true;
        if(!transport_completed){out.reason=Reason::kUncertainTransport;return out;}
        if(aggregate!=Aggregate::kOk&&aggregate!=Aggregate::kMultipleResults){out.reason=Reason::kUnusableAggregate;return out;}
        if(receipts.size()>out.count)return Malformed(out);
        std::array<bool,max_entries> seen{};
        for(const auto& receipt:receipts){
            // Failed entries may omit an ID. They cannot confirm any message;
            // every uncorrelated request entry remains retryable.
            if(receipt.message_id.empty()){
                if(receipt.positive)return Malformed(out);
                continue;
            }
            std::size_t index=0;
            while(index<out.count&&expected_ids[index]!=receipt.message_id)++index;
            if(index==out.count||seen[index])return Malformed(out);
            seen[index]=true;
            if(receipt.positive)out.entries[index]=Decision::kConfirmed;
        }
        out.response_usable=true;out.reason=Reason::kNone;
        return out;
    }

    // Qualified separately against the pinned Proxy5.5.0: a normal atomic
    // Broker batch returns one positive entry containing the exact comma-
    // joined client IDs. Keep this compatibility path explicit. Aggregate OK
    // alone, a prefix, duplicate/foreign IDs or partial joined results never
    // advance publication. Generic per-entry protocol users still use Map().
    static Result MapPinnedNormalBatch(
        const std::vector<std::string>& expected_ids,
        bool transport_completed,
        Aggregate aggregate,
        const std::vector<Entry>& receipts
    ) noexcept {
        auto out=Map(expected_ids,transport_completed,aggregate,{});
        if(!out.request_valid)return out;
        for(const auto& id:expected_ids){
            if(id.find(',')!=std::string::npos){
                Result invalid;invalid.count=out.count;return invalid;
            }
        }
        if(!transport_completed||out.reason==Reason::kUnusableAggregate)return out;
        bool joined=false;
        for(const auto& receipt:receipts)
            joined=joined||receipt.message_id.find(',')!=std::string::npos;
        if(!joined)return Map(expected_ids,transport_completed,aggregate,receipts);
        if(aggregate!=Aggregate::kOk||receipts.size()!=1||!receipts[0].positive)
            return Malformed(out);
        const auto& ids=receipts[0].message_id;
        if(ids.size()>max_entries*(max_message_id_bytes+1)-1)return Malformed(out);
        std::array<bool,max_entries> seen{};
        std::size_t begin=0,tokens=0;
        while(true){
            const auto separator=ids.find(',',begin);
            const auto length=(separator==std::string::npos?ids.size():separator)-begin;
            if(length==0||length>max_message_id_bytes||tokens>=out.count)
                return Malformed(out);
            std::size_t index=0;
            while(index<out.count&&
                (expected_ids[index].size()!=length||ids.compare(begin,length,expected_ids[index])!=0))++index;
            if(index==out.count||seen[index])return Malformed(out);
            seen[index]=true;++tokens;
            if(separator==std::string::npos)break;
            begin=separator+1;
        }
        if(tokens!=out.count)return Malformed(out);
        for(std::size_t i=0;i<out.count;++i)out.entries[i]=Decision::kConfirmed;
        out.response_usable=true;out.reason=Reason::kNone;return out;
    }
private:
    static Result Malformed(const Result& input) noexcept {
        // A late duplicate/foreign receipt invalidates earlier tentative
        // decisions too. No caller receives a partially trusted result.
        Result out;out.count=input.count;out.request_valid=input.request_valid;
        out.reason=Reason::kMalformedReceipts;return out;
    }
};

}}} // namespace tinyimx::eventing::rocketmq_transport
