#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace tinyimx {

// Dedicated ready-drain workers, inspired by the pinned WuKongIM BatchDB loop.
// There is no timed collection window and no blocked submitting caller. Each
// shard drains only already-available input, commits through execute(), and
// completes every accepted input exactly once. Global capacity includes work
// executing and completing, rather than only the queue length.
template<class Input, class Result>
class BoundedReadyBatch final {
public:
    enum class Admission { Accepted, Overloaded, Stopped };
    struct Options { std::size_t shards{4}, capacity{128}, batch{64}; };
    struct Stats {
        std::uint64_t accepted{0},completed{0},overloaded{0},stopped{0};
        std::uint64_t batches{0},execute_failures{0},completion_failures{0};
        std::size_t pending{0},peak_pending{0};
        std::array<std::uint64_t,65> batch_sizes{};
    };
    using Execute = std::function<std::vector<Result>(const std::vector<Input>&)>;
    using Completion = std::function<void(Result)>;

    BoundedReadyBatch(Options options, Execute execute, Result failure)
        : options_(options),execute_(std::move(execute)),failure_(std::move(failure)) {
        if(!execute_ || options.shards<1 || options.shards>16 ||
           options.capacity<1 || options.capacity>512 || options.batch<1 || options.batch>64)
            throw std::invalid_argument("invalid ready-batch bounds");
        for(std::size_t i=0;i<options.shards;++i)shards_.push_back(std::make_unique<Shard>());
        try {
            for(auto& shard:shards_)shard->worker=std::thread([this,s=shard.get()]{Run(*s);});
        }catch(...){Drain();throw;}
    }
    ~BoundedReadyBatch(){Drain();}
    BoundedReadyBatch(const BoundedReadyBatch&)=delete;
    BoundedReadyBatch& operator=(const BoundedReadyBatch&)=delete;

    // A rejected submission never invokes its completion. The caller owns the
    // explicit rejection response. Accepted callbacks must not throw; a faulty
    // callback is counted, never retried after a possible durable side effect.
    Admission Submit(std::uint64_t key,Input input,Completion complete) {
        if(!complete)throw std::invalid_argument("ready-batch completion missing");
        auto node=std::make_unique<Node>(Node{std::move(input),std::move(complete),failure_});
        std::lock_guard admission(admission_);
        if(!accepting_){++stopped_;return Admission::Stopped;}
        if(pending_.load(std::memory_order_relaxed)>=options_.capacity){++overloaded_;return Admission::Overloaded;}
        auto& shard=*shards_[key%shards_.size()];
        {
            std::lock_guard lock(shard.mutex);
            shard.queue.push_back(std::move(node));
            const auto value=++pending_;
            auto old=peak_.load(std::memory_order_relaxed);
            while(value>old&&!peak_.compare_exchange_weak(old,value,std::memory_order_relaxed)){}
            ++accepted_;
        }
        shard.ready.notify_one();
        return Admission::Accepted;
    }
    void Drain() noexcept {
        std::call_once(drain_,[this]{
            {
                std::lock_guard admission(admission_);accepting_=false;
                for(auto& shard:shards_){std::lock_guard lock(shard->mutex);shard->stopping=true;shard->ready.notify_one();}
            }
            for(auto& shard:shards_)if(shard->worker.joinable())shard->worker.join();
        });
    }
    Stats Snapshot()const noexcept {
        Stats s;s.accepted=accepted_;s.completed=completed_;s.overloaded=overloaded_;s.stopped=stopped_;
        s.pending=pending_;s.peak_pending=peak_;s.batches=batches_;s.execute_failures=execute_failures_;s.completion_failures=completion_failures_;
        for(std::size_t i=0;i<s.batch_sizes.size();++i)s.batch_sizes[i]=sizes_[i].load();
        return s;
    }
private:
    struct Node{Input input;Completion complete;Result failure;};
    struct Shard {
        std::mutex mutex;std::condition_variable ready;
        std::deque<std::unique_ptr<Node>>queue;bool stopping{false};std::thread worker;
    };
    void Run(Shard& shard)noexcept {
        // Allocate before processing any accepted request. If allocation fails,
        // drain individual requests with their preconstructed failure result.
        std::vector<std::unique_ptr<Node>> nodes;
        std::vector<Input> inputs;
        try{nodes.reserve(options_.batch);inputs.reserve(options_.batch);}catch(...){RunFailed(shard);return;}
        for(;;){
            {
                std::unique_lock lock(shard.mutex);
                shard.ready.wait(lock,[&]{return shard.stopping||!shard.queue.empty();});
                if(shard.queue.empty()&&shard.stopping)return;
                const auto count=std::min(options_.batch,shard.queue.size());
                for(std::size_t i=0;i<count;++i){nodes.push_back(std::move(shard.queue.front()));shard.queue.pop_front();}
            }
            bool valid=false;std::vector<Result> results;
            try {
                for(auto& node:nodes)inputs.push_back(node->input);
                results=execute_(inputs);valid=results.size()==nodes.size();
            }catch(...){/* An uncertain write is reported; never execute twice. */}
            ++batches_;++sizes_[nodes.size()];if(!valid)++execute_failures_;
            for(std::size_t i=0;i<nodes.size();++i){
                try{nodes[i]->complete(valid?std::move(results[i]):std::move(nodes[i]->failure));}catch(...){++completion_failures_;}
                ++completed_;--pending_;
            }
            inputs.clear();nodes.clear();
        }
    }
    void RunFailed(Shard& shard)noexcept {
        for(;;){
            std::unique_ptr<Node> node;
            {std::unique_lock lock(shard.mutex);shard.ready.wait(lock,[&]{return shard.stopping||!shard.queue.empty();});
             if(shard.queue.empty()&&shard.stopping)return;node=std::move(shard.queue.front());shard.queue.pop_front();}
            ++execute_failures_;
            try{node->complete(std::move(node->failure));}catch(...){++completion_failures_;}
            ++completed_;--pending_;
        }
    }
    Options options_;Execute execute_;Result failure_;
    std::vector<std::unique_ptr<Shard>>shards_;
    std::mutex admission_;std::once_flag drain_;bool accepting_{true};
    std::atomic<std::size_t>pending_{0},peak_{0};
    std::atomic<std::uint64_t>accepted_{0},completed_{0},overloaded_{0},stopped_{0};
    std::atomic<std::uint64_t>batches_{0},execute_failures_{0},completion_failures_{0};
    std::array<std::atomic<std::uint64_t>,65>sizes_{};
};
} // namespace tinyimx
