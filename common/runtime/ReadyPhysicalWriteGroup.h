#pragma once

#include "common/runtime/BoundedCallerBatch.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <stdexcept>
#include <vector>

namespace tinyimx {

// Caller-owned synchronous physical writes. Drain only already-ready inputs;
// no timer, detached thread, early completion or per-message durability change.
// The owner must retain its domain locks and keep this object alive until all
// admitted callers finish. Overflow uses the original single-write entrance.
template<class Input>
class ReadyPhysicalWriteGroup final {
public:
    struct Limits {
        std::size_t batch{64};
        std::size_t queued{128};
        std::size_t serialized_input_bytes{16 * 1024 * 1024};
    };
    struct Stats {
        std::uint64_t calls, admitted, execute_calls, grouped_inputs, fallbacks;
        std::size_t largest_group, held_input_bytes, queued;
    };
    explicit ReadyPhysicalWriteGroup(Limits limits={})
        :limits_(limits),batch_({limits.batch,limits.queued,std::chrono::microseconds(0)}) {
        if(limits.batch>64||limits.queued>256||!limits.serialized_input_bytes||
            limits.serialized_input_bytes>64*1024*1024)
            throw std::invalid_argument("invalid physical ready-write group bounds");
    }
    ReadyPhysicalWriteGroup(const ReadyPhysicalWriteGroup&)=delete;
    ReadyPhysicalWriteGroup&operator=(const ReadyPhysicalWriteGroup&)=delete;

    template<class Execute>
    void Run(Input input,std::size_t bytes,Execute&& execute) {
        ++calls_;
        if(!Reserve(bytes)){
            ++fallbacks_;
            Invoke(std::span<const Input>(&input,1),execute);
            return;
        }
        struct Release {
            std::atomic<std::size_t>& held;
            std::size_t bytes;
            ~Release(){held.fetch_sub(bytes,std::memory_order_relaxed);}
        } release{held_bytes_,bytes};
        ++admitted_;
        const Completion not_attempted{};
        auto outcome=batch_.Run(input,[&](const std::vector<Input>& inputs){
            // Allocate all completion slots before the physical call. The
            // underlying batcher's fallback then means genuinely not attempted.
            std::vector<Completion> results(inputs.size());
            std::exception_ptr error;
            try{Invoke(std::span<const Input>(inputs.data(),inputs.size()),execute);}
            catch(...){error=std::current_exception();}
            for(auto& result:results){result.attempted=true;result.error=error;}
            return results; // No allocation or throwing result move after write.
        },not_attempted);
        if(!outcome.attempted){
            // Admission/assembly failed before execute. An attempted exception
            // is always returned above and is NEVER replayed by this fallback.
            ++fallbacks_;
            Invoke(std::span<const Input>(&input,1),execute);
        }else if(outcome.error){
            std::rethrow_exception(outcome.error);
        }
    }
    Stats Snapshot()const {
        return {calls_.load(),admitted_.load(),execute_calls_.load(),grouped_inputs_.load(),
            fallbacks_.load(),largest_group_.load(),held_bytes_.load(),batch_.QueuedCount()};
    }
private:
    struct Completion {
        bool attempted{false};
        std::exception_ptr error;
    };
    bool Reserve(std::size_t bytes)noexcept {
        if(!bytes||bytes>limits_.serialized_input_bytes)return false;
        auto held=held_bytes_.load(std::memory_order_relaxed);
        while(bytes<=limits_.serialized_input_bytes-held){
            if(held_bytes_.compare_exchange_weak(held,held+bytes,std::memory_order_relaxed))return true;
        }
        return false;
    }
    template<class Execute>
    void Invoke(std::span<const Input> inputs,Execute& execute){
        ++execute_calls_;
        if(inputs.size()>1)grouped_inputs_.fetch_add(inputs.size(),std::memory_order_relaxed);
        auto previous=largest_group_.load(std::memory_order_relaxed);
        while(previous<inputs.size()&&!largest_group_.compare_exchange_weak(previous,inputs.size(),std::memory_order_relaxed)){}
        execute(inputs);
    }
    const Limits limits_;
    BoundedCallerBatch<Input,Completion> batch_;
    std::atomic<std::size_t> held_bytes_{0},largest_group_{0};
    std::atomic<std::uint64_t> calls_{0},admitted_{0},execute_calls_{0},grouped_inputs_{0},fallbacks_{0};
};
} // namespace tinyimx
