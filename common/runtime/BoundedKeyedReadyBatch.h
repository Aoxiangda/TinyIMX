#pragma once

#include "common/runtime/BoundedReadyBatch.h"
#include <exception>
#include <unordered_map>

namespace tinyimx {
// The legacy queue assigns a whole hash bucket to one blocking preparation
// worker. Here workers take ready keys from one bounded ring. A slow key holds
// its own FIFO lane, rather than unrelated keys with the same modulo value.
// There is no timer, new admission budget, or extra background thread.
template<class Input,class Result>
class BoundedKeyedReadyBatch final {
    using Legacy=BoundedReadyBatch<Input,Result>;
public:
    using Options=typename Legacy::Options;
    using Admission=typename Legacy::Admission;
    using Stats=typename Legacy::Stats;
    using Execute=typename Legacy::Execute;
    using Completion=typename Legacy::Completion;

    BoundedKeyedReadyBatch(Options options,Execute execute,Result failure)
        :options_(options),execute_(std::move(execute)),failure_(std::move(failure)) {
        if(!execute_||options.shards<1||options.shards>16||options.capacity<1||options.capacity>512||options.batch<1||options.batch>64)
            throw std::invalid_argument("invalid keyed ready-batch bounds");
        ready_.resize(options.capacity);lanes_.reserve(options.capacity);workers_.reserve(options.shards);
        try{for(std::size_t i=0;i<options.shards;++i)workers_.emplace_back([this]{Run();});}
        catch(...){Drain();throw;}
    }
    ~BoundedKeyedReadyBatch(){Drain();}
    BoundedKeyedReadyBatch(const BoundedKeyedReadyBatch&)=delete;
    BoundedKeyedReadyBatch& operator=(const BoundedKeyedReadyBatch&)=delete;

    Admission Submit(std::uint64_t key,Input input,Completion complete) {
        if(!complete)throw std::invalid_argument("keyed ready-batch completion missing");
        auto node=std::make_unique<Node>(Node{key,std::move(input),std::move(complete),failure_});
        std::lock_guard lock(mutex_);
        if(!accepting_){++stopped_;return Admission::Stopped;}
        if(pending_.load(std::memory_order_relaxed)>=options_.capacity){++overloaded_;return Admission::Overloaded;}
        const auto [it,inserted]=lanes_.try_emplace(key);
        try{it->second.queue.push_back(std::move(node));}
        catch(...){if(inserted)lanes_.erase(it);throw;}
        // Existing inactive lanes already have exactly one ready-ring entry.
        if(inserted)PushReady(key);
        const auto value=++pending_;auto old=peak_.load(std::memory_order_relaxed);
        while(value>old&&!peak_.compare_exchange_weak(old,value,std::memory_order_relaxed)){}
        ++accepted_;ready_cv_.notify_one();return Admission::Accepted;
    }
    void Drain()noexcept {
        std::call_once(drain_,[this]{
            {std::lock_guard lock(mutex_);accepting_=false;stopping_=true;}
            ready_cv_.notify_all();
            for(auto& thread:workers_)if(thread.joinable())thread.join();
        });
    }
    Stats Snapshot()const noexcept {
        Stats s;s.accepted=accepted_;s.completed=completed_;s.overloaded=overloaded_;s.stopped=stopped_;
        s.pending=pending_;s.peak_pending=peak_;s.batches=batches_;s.execute_failures=execute_failures_;s.completion_failures=completion_failures_;
        for(std::size_t i=0;i<s.batch_sizes.size();++i)s.batch_sizes[i]=sizes_[i].load();return s;
    }
private:
    struct Node{std::uint64_t key;Input input;Completion complete;Result failure;};
    struct Lane{std::deque<std::unique_ptr<Node>> queue;bool active{false};};
    void PushReady(std::uint64_t key)noexcept {
        // Ring capacity is allocated before accepting work. Releasing a key
        // after durable execution must never allocate or drop accepted work.
        if(ready_count_>=ready_.size())std::terminate();
        ready_[(ready_head_+ready_count_)%ready_.size()]=key;++ready_count_;
    }
    std::unique_ptr<Node> PopReady()noexcept {
        if(!ready_count_)std::terminate();
        const auto key=ready_[ready_head_];ready_head_=(ready_head_+1)%ready_.size();--ready_count_;
        auto it=lanes_.find(key);
        if(it==lanes_.end()||it->second.active||it->second.queue.empty())std::terminate();
        auto& lane=it->second;lane.active=true;
        auto node=std::move(lane.queue.front());lane.queue.pop_front();return node;
    }
    bool Take(std::vector<std::unique_ptr<Node>>& nodes)noexcept {
        std::unique_lock lock(mutex_);
        ready_cv_.wait(lock,[this]{return ready_count_||(stopping_&&pending_.load()==0);});
        if(!ready_count_)return false;
        const auto count=std::min(options_.batch,ready_count_);
        for(std::size_t i=0;i<count;++i)nodes.push_back(PopReady());return true;
    }
    void Complete(Node& node,Result& result)noexcept {
        try{node.complete(std::move(result));}catch(...){++completion_failures_;}
        {
            std::lock_guard lock(mutex_);auto it=lanes_.find(node.key);
            if(it==lanes_.end()||!it->second.active)std::terminate();
            it->second.active=false;
            if(it->second.queue.empty())lanes_.erase(it);else PushReady(node.key);
            ++completed_;--pending_;
        }
        ready_cv_.notify_all();
    }
    void Run()noexcept {
        std::vector<std::unique_ptr<Node>>nodes;std::vector<Input>inputs;
        try{nodes.reserve(options_.batch);inputs.reserve(options_.batch);}catch(...){RunFailed();return;}
        while(Take(nodes)){
            bool valid=false;std::vector<Result>results;
            try{for(auto& node:nodes)inputs.push_back(node->input);results=execute_(inputs);valid=results.size()==nodes.size();}
            catch(...){/* An uncertain mutation is reported once, never retried. */}
            ++batches_;++sizes_[nodes.size()];if(!valid)++execute_failures_;
            for(std::size_t i=0;i<nodes.size();++i)Complete(*nodes[i],valid?results[i]:nodes[i]->failure);
            inputs.clear();nodes.clear();
        }
    }
    void RunFailed()noexcept {
        for(;;){
            std::unique_ptr<Node>node;
            {
                std::unique_lock lock(mutex_);ready_cv_.wait(lock,[this]{return ready_count_||(stopping_&&pending_.load()==0);});
                if(!ready_count_)return;node=PopReady();
            }
            ++execute_failures_;Complete(*node,node->failure);
        }
    }
    Options options_;Execute execute_;Result failure_;
    std::mutex mutex_;std::condition_variable ready_cv_;std::once_flag drain_;
    bool accepting_{true},stopping_{false};std::vector<std::thread>workers_;
    std::unordered_map<std::uint64_t,Lane>lanes_;
    std::vector<std::uint64_t>ready_;std::size_t ready_head_{0},ready_count_{0};
    std::atomic<std::size_t>pending_{0},peak_{0};
    std::atomic<std::uint64_t>accepted_{0},completed_{0},overloaded_{0},stopped_{0},batches_{0},execute_failures_{0},completion_failures_{0};
    std::array<std::atomic<std::uint64_t>,65>sizes_{};
};
} // namespace tinyimx
