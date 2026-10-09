#include "tests/concurrency/TestFramework.h"

#include "common/net/Channel.h"
#include "common/net/EventLoop.h"
#include "common/net/EventLoopThread.h"
#include "common/net/EventLoopThreadPool.h"
#include "common/net/Socket.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

namespace tinyimx::test {

void RegisterEventLoopTests(TestRunner& runner) {
    runner.Add("EventLoop.PipeReadableEvent", []() {
        int pipe_fd[2] = {-1, -1};

        TINYIMX_EXPECT_EQ(::pipe(pipe_fd), 0);

        Socket::SetNonBlocking(pipe_fd[0]);
        Socket::SetCloseOnExec(pipe_fd[0]);
        Socket::SetCloseOnExec(pipe_fd[1]);

        EventLoop loop;
        TINYIMX_EXPECT_TRUE(loop.IsValid());

        Channel channel(&loop, pipe_fd[0]);

        std::atomic<int> read_count{0};

        channel.SetReadCallback([&]() {
            char buffer[128];

            while (true) {
                const ssize_t n = ::read(
                    pipe_fd[0],
                    buffer,
                    sizeof(buffer)
                );

                if (n > 0) {
                    read_count.fetch_add(1, std::memory_order_relaxed);
                    channel.DisableAll();
                    loop.Quit();
                    return;
                }

                if (n == 0) {
                    channel.DisableAll();
                    loop.Quit();
                    return;
                }

                if (errno == EINTR) {
                    continue;
                }

                if (errno == EAGAIN ||
                    errno == EWOULDBLOCK) {
                    return;
                }

                channel.DisableAll();
                loop.Quit();
                return;
            }
        });

        channel.EnableReading();

        std::thread writer_thread([&]() {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(100)
            );

            const std::string message =
                "event-loop-test";

            ::write(
                pipe_fd[1],
                message.data(),
                message.size()
            );
        });

        loop.Loop();

        if (writer_thread.joinable()) {
            writer_thread.join();
        }

        loop.RemoveChannel(&channel);

        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);

        TINYIMX_EXPECT_EQ(
            read_count.load(),
            1
        );
    });

    runner.Add("EventLoop.RunInLoopSameThread", []() {
        EventLoop loop;

        TINYIMX_EXPECT_TRUE(loop.IsValid());
        TINYIMX_EXPECT_TRUE(loop.IsInLoopThread());

        const std::thread::id caller_thread_id =
            std::this_thread::get_id();

        std::thread::id callback_thread_id;

        bool callback_executed = false;
        loop.RunInLoop([&]() {
            callback_executed = true;

            callback_thread_id =
                std::this_thread::get_id();
        });

        TINYIMX_EXPECT_TRUE(
            callback_executed
        );

        TINYIMX_EXPECT_TRUE(
            callback_thread_id ==
            caller_thread_id
        );
    });

    runner.Add("EventLoop.RunInLoopCrossThread", []() {
        std::mutex mutex;
        std::condition_variable condition;

        EventLoop* worker_loop = nullptr;

        bool loop_ready = false;
        bool loop_valid = false;
        bool callback_executed = false;

        std::thread::id loop_thread_id;
        std::thread::id callback_thread_id;

        std::thread loop_thread([&]() {
            EventLoop loop;

            const bool valid =
                loop.IsValid();

            {
                std::lock_guard<std::mutex> lock(
                    mutex
                );

                loop_valid = valid;

                if (valid) {
                    worker_loop = &loop;
                }

                loop_thread_id =
                    std::this_thread::get_id();

                loop_ready = true;
            }

            condition.notify_one();

            if (!valid) {
                return;
            }

            loop.Loop();
        });

        EventLoop* loop = nullptr;

        {
            std::unique_lock<std::mutex> lock(
                mutex
            );

            condition.wait(
                lock,
                [&]() {
                    return loop_ready;
                }
            );

            loop = worker_loop;
        }

        bool completed_within_timeout = false;

        if (loop != nullptr) {
            loop->RunInLoop([&, loop]() {
                {
                    std::lock_guard<std::mutex> lock(
                        mutex
                    );

                    callback_thread_id =
                        std::this_thread::get_id();

                    callback_executed = true;
                }

                condition.notify_one();

                loop->Quit();
            });

            {
                std::unique_lock<std::mutex> lock(
                    mutex
                );

                completed_within_timeout =
                    condition.wait_for(
                        lock,
                        std::chrono::seconds(2),
                        [&]() {
                            return callback_executed;
                        }
                    );
            }

            if (!completed_within_timeout) {
                loop->Quit();
            }
        }

        if (loop_thread.joinable()) {
            loop_thread.join();
        }

        TINYIMX_EXPECT_TRUE(
            loop_valid
        );

        TINYIMX_EXPECT_TRUE(
            completed_within_timeout
        );

        TINYIMX_EXPECT_TRUE(
            callback_executed
        );

        TINYIMX_EXPECT_TRUE(
            callback_thread_id ==
            loop_thread_id
        );
    });

    runner.Add("EventLoop.QueueInLoopNested", []() {
        std::mutex mutex;
        std::condition_variable condition;

        EventLoop* worker_loop = nullptr;

        bool loop_ready = false;
        bool loop_valid = false;

        bool first_callback_executed = false;
        bool second_callback_executed = false;

        std::thread::id loop_thread_id;
        std::thread::id first_callback_thread_id;
        std::thread::id second_callback_thread_id;

        std::thread loop_thread([&]() {
            EventLoop loop;

            const bool valid =
                loop.IsValid();

            {
                std::lock_guard<std::mutex> lock(
                    mutex
                );

                loop_valid = valid;

                if (valid) {
                    worker_loop = &loop;
                }

                loop_thread_id =
                    std::this_thread::get_id();

                loop_ready = true;
            }

            condition.notify_one();

            if (!valid) {
                return;
            }

            loop.Loop();
        });

        EventLoop* loop = nullptr;

        {
            std::unique_lock<std::mutex> lock(
                mutex
            );

            condition.wait(
                lock,
                [&]() {
                    return loop_ready;
                }
            );

            loop = worker_loop;
        }

        bool completed_within_timeout = false;

        if (loop != nullptr) {
            loop->RunInLoop([&, loop]() {
                {
                    std::lock_guard<std::mutex> lock(
                        mutex
                    );

                    first_callback_executed = true;

                    first_callback_thread_id =
                        std::this_thread::get_id();
                }

                loop->QueueInLoop([&, loop]() {
                    {
                        std::lock_guard<std::mutex> lock(
                            mutex
                        );

                        second_callback_executed = true;

                        second_callback_thread_id =
                            std::this_thread::get_id();
                    }

                    condition.notify_one();

                    loop->Quit();
                });
            });

            {
                std::unique_lock<std::mutex> lock(
                    mutex
                );

                completed_within_timeout =
                    condition.wait_for(
                        lock,
                        std::chrono::seconds(2),
                        [&]() {
                            return second_callback_executed;
                        }
                    );
            }

            if (!completed_within_timeout) {
                loop->Quit();
            }
        }

        if (loop_thread.joinable()) {
            loop_thread.join();
        }

        TINYIMX_EXPECT_TRUE(
            loop_valid
        );

        TINYIMX_EXPECT_TRUE(
            completed_within_timeout
        );

        TINYIMX_EXPECT_TRUE(
            first_callback_executed
        );

        TINYIMX_EXPECT_TRUE(
            second_callback_executed
        );

        TINYIMX_EXPECT_TRUE(
            first_callback_thread_id ==
            loop_thread_id
        );

        TINYIMX_EXPECT_TRUE(
            second_callback_thread_id ==
            loop_thread_id
        );
    });

    runner.Add(
    "EventLoop.RunAfterOneShot",
    []() {
        EventLoop loop;

        TINYIMX_EXPECT_TRUE(
            loop.IsValid()
        );

        TINYIMX_EXPECT_TRUE(
            loop.IsInLoopThread()
        );

        std::mutex mutex;
        std::condition_variable
            condition;

        bool callback_fired = false;
        bool callback_in_loop_thread =
            false;

        bool watchdog_timed_out =
            false;

        std::size_t callback_count = 0;

        std::chrono::milliseconds
            elapsed{0};

        const auto started_at =
            std::chrono::steady_clock::
                now();

        const TimerId timer_id =
            loop.RunAfter(
                std::chrono::
                    milliseconds(100),
                [&]() {
                    {
                        std::lock_guard<
                            std::mutex
                        > lock(mutex);

                        callback_fired =
                            true;

                        callback_in_loop_thread =
                            loop.IsInLoopThread();

                        ++callback_count;

                        elapsed =
                            std::chrono::
                                duration_cast<
                                    std::chrono::
                                        milliseconds
                                >(
                                    std::chrono::
                                        steady_clock::
                                            now() -
                                    started_at
                                );
                    }

                    condition.notify_one();

                    loop.Quit();
                }
            );

        TINYIMX_EXPECT_TRUE(
            timer_id.IsValid()
        );

        std::thread watchdog([&]() {
            std::unique_lock<std::mutex>
                lock(mutex);

            const bool completed =
                condition.wait_for(
                    lock,
                    std::chrono::seconds(2),
                    [&]() {
                        return callback_fired;
                    }
                );

            if (completed) {
                return;
            }

            watchdog_timed_out = true;

            lock.unlock();

            loop.Quit();
        });

        loop.Loop();

        if (watchdog.joinable()) {
            watchdog.join();
        }

        bool fired_snapshot = false;
        bool owner_snapshot = false;
        bool timeout_snapshot = false;

        std::size_t
            count_snapshot = 0;

        std::chrono::milliseconds
            elapsed_snapshot{0};

        {
            std::lock_guard<std::mutex>
                lock(mutex);

            fired_snapshot =
                callback_fired;

            owner_snapshot =
                callback_in_loop_thread;

            timeout_snapshot =
                watchdog_timed_out;

            count_snapshot =
                callback_count;

            elapsed_snapshot =
                elapsed;
        }

        TINYIMX_EXPECT_TRUE(
            fired_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            owner_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            !timeout_snapshot
        );

        TINYIMX_EXPECT_EQ(
            count_snapshot,
            static_cast<std::size_t>(1)
        );

        TINYIMX_EXPECT_TRUE(
            elapsed_snapshot >=
            std::chrono::
                milliseconds(50)
        );

        TINYIMX_EXPECT_TRUE(
            elapsed_snapshot <
            std::chrono::seconds(2)
        );
    }
);

runner.Add(
    "EventLoop.RunEveryAndCancel",
    []() {
        EventLoop loop;

        TINYIMX_EXPECT_TRUE(
            loop.IsValid()
        );

        std::mutex mutex;
        std::condition_variable
            condition;

        std::size_t callback_count = 0;

        bool callbacks_in_loop_thread =
            true;

        bool cancel_accepted = false;
        bool verification_fired = false;
        bool watchdog_timed_out = false;

        TimerId repeating_id;

        repeating_id =
            loop.RunEvery(
                std::chrono::
                    milliseconds(50),
                [&]() {
                    bool should_cancel =
                        false;

                    {
                        std::lock_guard<
                            std::mutex
                        > lock(mutex);

                        ++callback_count;

                        if (!loop.
                            IsInLoopThread()) {
                            callbacks_in_loop_thread =
                                false;
                        }

                        should_cancel =
                            callback_count == 3;
                    }

                    if (should_cancel) {
                        const bool accepted =
                            loop.Cancel(
                                repeating_id
                            );

                        std::lock_guard<
                            std::mutex
                        > lock(mutex);

                        cancel_accepted =
                            accepted;
                    }
                }
            );

        const TimerId verification_id =
            loop.RunAfter(
                std::chrono::
                    milliseconds(350),
                [&]() {
                    {
                        std::lock_guard<
                            std::mutex
                        > lock(mutex);

                        verification_fired =
                            true;
                    }

                    condition.notify_one();

                    loop.Quit();
                }
            );

        TINYIMX_EXPECT_TRUE(
            repeating_id.IsValid()
        );

        TINYIMX_EXPECT_TRUE(
            verification_id.IsValid()
        );

        std::thread watchdog([&]() {
            std::unique_lock<std::mutex>
                lock(mutex);

            const bool completed =
                condition.wait_for(
                    lock,
                    std::chrono::seconds(2),
                    [&]() {
                        return
                            verification_fired;
                    }
                );

            if (completed) {
                return;
            }

            watchdog_timed_out = true;

            lock.unlock();

            loop.Quit();
        });

        loop.Loop();

        if (watchdog.joinable()) {
            watchdog.join();
        }

        std::size_t count_snapshot = 0;

        bool owner_snapshot = false;
        bool cancel_snapshot = false;
        bool verification_snapshot = false;
        bool timeout_snapshot = false;

        {
            std::lock_guard<std::mutex>
                lock(mutex);

            count_snapshot =
                callback_count;

            owner_snapshot =
                callbacks_in_loop_thread;

            cancel_snapshot =
                cancel_accepted;

            verification_snapshot =
                verification_fired;

            timeout_snapshot =
                watchdog_timed_out;
        }

        TINYIMX_EXPECT_EQ(
            count_snapshot,
            static_cast<std::size_t>(3)
        );

        TINYIMX_EXPECT_TRUE(
            owner_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            cancel_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            verification_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            !timeout_snapshot
        );
    }
);

runner.Add(
    "EventLoop.CancelBeforeExpiration",
    []() {
        EventLoop loop;

        TINYIMX_EXPECT_TRUE(
            loop.IsValid()
        );

        std::mutex mutex;
        std::condition_variable
            condition;

        std::size_t canceled_callback_count =
            0;

        bool verification_fired = false;
        bool watchdog_timed_out = false;

        const TimerId canceled_id =
            loop.RunAfter(
                std::chrono::
                    milliseconds(120),
                [&]() {
                    std::lock_guard<
                        std::mutex
                    > lock(mutex);

                    ++canceled_callback_count;
                }
            );

        TINYIMX_EXPECT_TRUE(
            canceled_id.IsValid()
        );

        const bool cancel_accepted =
            loop.Cancel(
                canceled_id
            );

        const TimerId verification_id =
            loop.RunAfter(
                std::chrono::
                    milliseconds(260),
                [&]() {
                    {
                        std::lock_guard<
                            std::mutex
                        > lock(mutex);

                        verification_fired =
                            true;
                    }

                    condition.notify_one();

                    loop.Quit();
                }
            );

        TINYIMX_EXPECT_TRUE(
            cancel_accepted
        );

        TINYIMX_EXPECT_TRUE(
            verification_id.IsValid()
        );

        std::thread watchdog([&]() {
            std::unique_lock<std::mutex>
                lock(mutex);

            const bool completed =
                condition.wait_for(
                    lock,
                    std::chrono::seconds(2),
                    [&]() {
                        return
                            verification_fired;
                    }
                );

            if (completed) {
                return;
            }

            watchdog_timed_out = true;

            lock.unlock();

            loop.Quit();
        });

        loop.Loop();

        if (watchdog.joinable()) {
            watchdog.join();
        }

        std::size_t count_snapshot = 0;

        bool verification_snapshot = false;
        bool timeout_snapshot = false;

        {
            std::lock_guard<std::mutex>
                lock(mutex);

            count_snapshot =
                canceled_callback_count;

            verification_snapshot =
                verification_fired;

            timeout_snapshot =
                watchdog_timed_out;
        }

        TINYIMX_EXPECT_EQ(
            count_snapshot,
            static_cast<std::size_t>(0)
        );

        TINYIMX_EXPECT_TRUE(
            verification_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            !timeout_snapshot
        );
    }
);

runner.Add(
    "EventLoop.CrossThreadAddAndCancelTimer",
    []() {
        EventLoop loop;

        TINYIMX_EXPECT_TRUE(
            loop.IsValid()
        );

        std::mutex mutex;
        std::condition_variable
            condition;

        std::size_t canceled_callback_count =
            0;

        bool canceled_id_valid = false;
        bool finish_id_valid = false;
        bool cancel_accepted = false;

        bool finish_callback_fired = false;

        bool finish_callback_in_loop_thread =
            false;

        bool watchdog_timed_out = false;

        std::thread worker([&]() {
            std::this_thread::sleep_for(
                std::chrono::
                    milliseconds(30)
            );

            const TimerId canceled_id =
                loop.RunAfter(
                    std::chrono::
                        milliseconds(150),
                    [&]() {
                        std::lock_guard<
                            std::mutex
                        > lock(mutex);

                        ++canceled_callback_count;
                    }
                );

            const bool accepted =
                loop.Cancel(
                    canceled_id
                );

            const TimerId finish_id =
                loop.RunAfter(
                    std::chrono::
                        milliseconds(300),
                    [&]() {
                        {
                            std::lock_guard<
                                std::mutex
                            > lock(mutex);

                            finish_callback_fired =
                                true;

                            finish_callback_in_loop_thread =
                                loop.
                                    IsInLoopThread();
                        }

                        condition.notify_one();

                        loop.Quit();
                    }
                );

            {
                std::lock_guard<std::mutex>
                    lock(mutex);

                canceled_id_valid =
                    canceled_id.IsValid();

                finish_id_valid =
                    finish_id.IsValid();

                cancel_accepted =
                    accepted;
            }
        });

        std::thread watchdog([&]() {
            std::unique_lock<std::mutex>
                lock(mutex);

            const bool completed =
                condition.wait_for(
                    lock,
                    std::chrono::seconds(2),
                    [&]() {
                        return
                            finish_callback_fired;
                    }
                );

            if (completed) {
                return;
            }

            watchdog_timed_out = true;

            lock.unlock();

            loop.Quit();
        });

        loop.Loop();

        if (worker.joinable()) {
            worker.join();
        }

        if (watchdog.joinable()) {
            watchdog.join();
        }

        std::size_t count_snapshot = 0;

        bool canceled_id_snapshot = false;
        bool finish_id_snapshot = false;
        bool cancel_snapshot = false;
        bool finish_snapshot = false;
        bool owner_snapshot = false;
        bool timeout_snapshot = false;

        {
            std::lock_guard<std::mutex>
                lock(mutex);

            count_snapshot =
                canceled_callback_count;

            canceled_id_snapshot =
                canceled_id_valid;

            finish_id_snapshot =
                finish_id_valid;

            cancel_snapshot =
                cancel_accepted;

            finish_snapshot =
                finish_callback_fired;

            owner_snapshot =
                finish_callback_in_loop_thread;

            timeout_snapshot =
                watchdog_timed_out;
        }

        TINYIMX_EXPECT_EQ(
            count_snapshot,
            static_cast<std::size_t>(0)
        );

        TINYIMX_EXPECT_TRUE(
            canceled_id_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            finish_id_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            cancel_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            finish_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            owner_snapshot
        );

        TINYIMX_EXPECT_TRUE(
            !timeout_snapshot
        );
    }
);


runner.Add(
    "EventLoopThread.StartLoopAndDispatch",
    []() {
        struct CallbackResult {
            bool in_loop_thread{false};
            std::thread::id thread_id;
        };

        const std::thread::id caller_thread_id =
            std::this_thread::get_id();

        std::promise<CallbackResult>
            callback_promise;

        std::future<CallbackResult>
            callback_future =
                callback_promise.get_future();

        bool completed_within_timeout = false;

        CallbackResult callback_result;

        {
            EventLoopThread loop_thread;

            EventLoop* loop =
                loop_thread.StartLoop();

            TINYIMX_EXPECT_TRUE(
                loop != nullptr
            );

            TINYIMX_EXPECT_TRUE(
                loop->IsValid()
            );

            TINYIMX_EXPECT_TRUE(
                !loop->IsInLoopThread()
            );

            loop->RunInLoop(
                [loop, &callback_promise]() {
                    CallbackResult result;

                    result.in_loop_thread =
                        loop->IsInLoopThread();

                    result.thread_id =
                        std::this_thread::
                            get_id();

                    callback_promise.set_value(
                        result
                    );
                }
            );

            if (callback_future.wait_for(
                    std::chrono::seconds(2)
                ) ==
                std::future_status::ready) {
                callback_result =
                    callback_future.get();

                completed_within_timeout =
                    true;
            }
        }

        TINYIMX_EXPECT_TRUE(
            completed_within_timeout
        );

        TINYIMX_EXPECT_TRUE(
            callback_result.in_loop_thread
        );

        TINYIMX_EXPECT_TRUE(
            callback_result.thread_id !=
            caller_thread_id
        );
    }
);

runner.Add(
    "EventLoopThreadPool.RoundRobinAndDispatch",
    []() {
        constexpr std::size_t kThreadCount = 3;

        struct CallbackResult {
            bool in_loop_thread{false};
            std::thread::id thread_id;
        };

        const std::thread::id caller_thread_id =
            std::this_thread::get_id();

        EventLoop base_loop;

        TINYIMX_EXPECT_TRUE(
            base_loop.IsValid()
        );

        TINYIMX_EXPECT_TRUE(
            base_loop.IsInLoopThread()
        );

        std::vector<std::promise<CallbackResult>>
            callback_promises(
                kThreadCount
            );

        std::vector<std::future<CallbackResult>>
            callback_futures;

        callback_futures.reserve(
            kThreadCount
        );

        for (auto& promise :
             callback_promises) {
            callback_futures.push_back(
                promise.get_future()
            );
        }

        std::vector<CallbackResult>
            callback_results(
                kThreadCount
            );

        bool completed_within_timeout =
            true;

        {
            EventLoopThreadPool pool(
                &base_loop,
                kThreadCount
            );

            TINYIMX_EXPECT_TRUE(
                !pool.Started()
            );

            TINYIMX_EXPECT_EQ(
                pool.ThreadCount(),
                kThreadCount
            );

            TINYIMX_EXPECT_TRUE(
                pool.Start()
            );

            TINYIMX_EXPECT_TRUE(
                pool.Started()
            );

            const std::vector<EventLoop*>
                loops =
                    pool.GetAllLoops();

            TINYIMX_EXPECT_EQ(
                loops.size(),
                kThreadCount
            );

            for (EventLoop* loop :
                 loops) {
                TINYIMX_EXPECT_TRUE(
                    loop != nullptr
                );

                TINYIMX_EXPECT_TRUE(
                    loop->IsValid()
                );

                TINYIMX_EXPECT_TRUE(
                    !loop->IsInLoopThread()
                );
            }

            EventLoop* first =
                pool.GetNextLoop();

            EventLoop* second =
                pool.GetNextLoop();

            EventLoop* third =
                pool.GetNextLoop();

            EventLoop* fourth =
                pool.GetNextLoop();

            TINYIMX_EXPECT_TRUE(
                first == loops[0]
            );

            TINYIMX_EXPECT_TRUE(
                second == loops[1]
            );

            TINYIMX_EXPECT_TRUE(
                third == loops[2]
            );

            TINYIMX_EXPECT_TRUE(
                fourth == loops[0]
            );

            for (std::size_t i = 0;
                 i < kThreadCount;
                 ++i) {
                EventLoop* loop =
                    loops[i];

                auto* callback_promise =
                    &callback_promises[i];

                loop->RunInLoop(
                    [
                        loop,
                        callback_promise
                    ]() {
                        CallbackResult result;

                        result.in_loop_thread =
                            loop->
                                IsInLoopThread();

                        result.thread_id =
                            std::this_thread::
                                get_id();

                        callback_promise->
                            set_value(
                                result
                            );
                    }
                );
            }

            const auto deadline =
                std::chrono::
                    steady_clock::now() +
                std::chrono::seconds(2);

            for (std::size_t i = 0;
                 i < kThreadCount;
                 ++i) {
                if (callback_futures[i].
                        wait_until(
                            deadline
                        ) !=
                    std::future_status::
                        ready) {
                    completed_within_timeout =
                        false;

                    break;
                }

                callback_results[i] =
                    callback_futures[i].
                        get();
            }
        }

        TINYIMX_EXPECT_TRUE(
            completed_within_timeout
        );

        std::set<std::thread::id>
            callback_thread_ids;

        for (const auto& result :
             callback_results) {
            TINYIMX_EXPECT_TRUE(
                result.in_loop_thread
            );

            TINYIMX_EXPECT_TRUE(
                result.thread_id !=
                caller_thread_id
            );

            callback_thread_ids.insert(
                result.thread_id
            );
        }

        TINYIMX_EXPECT_EQ(
            callback_thread_ids.size(),
            kThreadCount
        );
    }
);

runner.Add(
    "EventLoopThreadPool.ZeroThreadFallback",
    []() {
        EventLoop base_loop;

        TINYIMX_EXPECT_TRUE(
            base_loop.IsValid()
        );

        EventLoopThreadPool pool(
            &base_loop,
            0
        );

        TINYIMX_EXPECT_TRUE(
            pool.Start()
        );

        TINYIMX_EXPECT_TRUE(
            pool.Started()
        );

        TINYIMX_EXPECT_EQ(
            pool.ThreadCount(),
            static_cast<std::size_t>(0)
        );

        EventLoop* loop =
            pool.GetNextLoop();

        TINYIMX_EXPECT_TRUE(
            loop == &base_loop
        );

        const std::vector<EventLoop*> loops =
            pool.GetAllLoops();

        TINYIMX_EXPECT_EQ(
            loops.size(),
            static_cast<std::size_t>(1)
        );

        TINYIMX_EXPECT_TRUE(
            loops[0] == &base_loop
        );
    }
);

}


}  // namespace tinyimx::test