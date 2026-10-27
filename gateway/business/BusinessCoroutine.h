#pragma once
#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace tinyimx {
// One lazy, owning frame per accepted logical task; no OS thread per RPC.
class BusinessCoroutine {
public:
    using Job=std::function<void()>;
    using Post=std::function<void(Job)>;
    using Around=std::function<void(Job)>;
    using Done=std::function<void(std::exception_ptr)>;
    using Schedule=std::function<void(std::coroutine_handle<>)>;
    struct promise_type {
        std::exception_ptr error;
        BusinessCoroutine get_return_object(){return BusinessCoroutine{Handle::from_promise(*this)};}
        std::suspend_always initial_suspend()noexcept{return {};}
        std::suspend_always final_suspend()noexcept{return {};}
        void return_void()noexcept{}
        void unhandled_exception()noexcept{error=std::current_exception();}
    };
    using Handle=std::coroutine_handle<promise_type>;
    explicit BusinessCoroutine(Handle h):handle_(h){}
    BusinessCoroutine(BusinessCoroutine&& other)noexcept:handle_(std::exchange(other.handle_,{})){}
    BusinessCoroutine(const BusinessCoroutine&)=delete;
    ~BusinessCoroutine(){if(handle_)handle_.destroy();}
    void RunSync(){
        handle_.resume();
        if(!handle_.done())throw std::logic_error("synchronous business coroutine suspended");
        if(handle_.promise().error)std::rethrow_exception(handle_.promise().error);
    }
    void Start(Post post,Around around,Done done)&&;
    static Schedule CurrentSchedule(){
        if(!schedule_)throw std::logic_error("RPC await outside business coroutine runner");
        return schedule_;
    }
private:
    class Runner;
    inline static thread_local Schedule schedule_;
    Handle handle_;
};

class BusinessCoroutine::Runner final:public std::enable_shared_from_this<Runner>{
public:
    Runner(Handle h,Post post,Around around,Done done)
        :root_(h),post_(std::move(post)),around_(std::move(around)),done_(std::move(done)){}
    ~Runner(){if(root_)root_.destroy();}
    void Start(){keepalive_=shared_from_this();running_=true;Run(root_);}
private:
    void Request(std::coroutine_handle<> h){
        {
            std::lock_guard lock(mutex_);
            if(finished_)return;
            if(ready_)throw std::logic_error("multiple outstanding resumes for one business frame");
            ready_=h;
            if(running_||scheduled_)return;
            scheduled_=true;
        }
        auto self=shared_from_this();
        post_([self]{
            std::coroutine_handle<> next;
            {std::lock_guard lock(self->mutex_);self->scheduled_=false;self->running_=true;
             next=std::exchange(self->ready_,{});}
            self->Run(next);
        });
    }
    void Run(std::coroutine_handle<> h){
        auto self=shared_from_this(); // keep alive across done releasing all task owners
        for(;;){
            auto previous=std::move(schedule_);
            schedule_=[weak=weak_from_this()](std::coroutine_handle<> next){
                if(auto runner=weak.lock())runner->Request(next);
            };
            std::exception_ptr error;
            try {
                if(around_)around_([h]{h.resume();});else h.resume();
            }catch(...){error=std::current_exception();}
            schedule_=std::move(previous);
            if(error||root_.done()){
                if(!error)error=root_.promise().error;
                {std::lock_guard lock(mutex_);finished_=true;running_=false;ready_={};}
                auto done=std::move(done_);
                root_.destroy();root_={};keepalive_.reset();
                done(error);return;
            }
            {std::lock_guard lock(mutex_);
             if(ready_){h=std::exchange(ready_,{});continue;}
             running_=false;return;}
        }
    }
    Handle root_;Post post_;Around around_;Done done_;
    std::shared_ptr<Runner> keepalive_;
    std::mutex mutex_;std::coroutine_handle<> ready_;
    bool running_{false},scheduled_{false},finished_{false};
};
inline void BusinessCoroutine::Start(Post post,Around around,Done done)&&{
    auto runner=std::make_shared<Runner>(handle_,std::move(post),std::move(around),std::move(done));
    handle_={};runner->Start();
}
} // namespace tinyimx
