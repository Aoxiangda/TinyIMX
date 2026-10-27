#pragma once
#include "gateway/business/BusinessCoroutine.h"
#include <atomic>

namespace tinyimx::rpc {
// With GCC11, construct starters owning nontrivial captures outside the co_await
// expression, using a named awaiter or an ordinary awaiter factory (PERF-903).
// The runner owns the coroutine until its continuation drains.
template<class T>class RpcAwaitable final {
public:
    using Callback=std::function<void(T)>;
    using Start=std::function<void(Callback)>;
    explicit RpcAwaitable(T ready):state_(std::make_shared<State>()){
        state_->value.emplace(std::move(ready));
    }
    RpcAwaitable(bool asynchronous,std::function<T()> sync,Start start)
        :state_(std::make_shared<State>()),start_(std::move(start)){
        if(!asynchronous){state_->value.emplace(sync());start_={};}
    }
    bool await_ready()const noexcept{return state_->value.has_value();}
    void await_suspend(std::coroutine_handle<> frame){
        auto schedule=BusinessCoroutine::CurrentSchedule();auto state=state_;
        start_([state,frame,schedule=std::move(schedule)](T value)mutable {
            if(state->completed.exchange(true,std::memory_order_acq_rel))return;
            state->value.emplace(std::move(value));
            // The runner serializes even inline/racing RPC completion with the
            // initial resume; callbacks never execute business work directly.
            schedule(frame);
        });
        start_={};
    }
    T await_resume(){return std::move(state_->value.value());}
private:
    struct State{std::atomic<bool> completed{false};std::optional<T> value;};
    std::shared_ptr<State> state_;Start start_;
};
} // namespace tinyimx::rpc
