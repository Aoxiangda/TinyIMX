#include "common/config/Config.h"
#include "common/concurrency/ThreadPool.h"
#include "common/logging/LogMacros.h"
#include "common/logging/Logger.h"

#include <atomic>
#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void PrintStats(const std::string& title, const tinyimx::ThreadPoolStats& stats) {
    std::cout << "\n========== " << title << " ==========\n";
    std::cout << "state = " << tinyimx::ThreadPoolStateToString(stats.state) << '\n';
    std::cout << "submitted_task_count = " << stats.submitted_task_count << '\n';
    std::cout << "completed_task_count = " << stats.completed_task_count << '\n';
    std::cout << "failed_task_count = " << stats.failed_task_count << '\n';
    std::cout << "rejected_task_count = " << stats.rejected_task_count << '\n';
    std::cout << "discarded_task_count = " << stats.discarded_task_count << '\n';
    std::cout << "overwritten_task_count = " << stats.overwritten_task_count << '\n';
    std::cout << "shutdown_discarded_task_count = " << stats.shutdown_discarded_task_count << '\n';
    std::cout << "worker_count = " << stats.worker_count << '\n';
    std::cout << "active_thread_count = " << stats.active_thread_count << '\n';
    std::cout << "dynamic_resize_enabled = "<< (stats.dynamic_resize_enabled ? "true" : "false") << '\n';
    std::cout << "current_worker_count = " << stats.current_worker_count << '\n';
    std::cout << "peak_worker_count = " << stats.peak_worker_count << '\n';
    std::cout << "threads_created_count = " << stats.threads_created_count << '\n';
    std::cout << "threads_destroyed_count = " << stats.threads_destroyed_count << '\n';
    std::cout << "dynamic_resize_count = " << stats.dynamic_resize_count << '\n';
    std::cout << "current_queue_size = " << stats.current_queue_size << '\n';
    std::cout << "queue_capacity = " << stats.queue_capacity << '\n';
    std::cout << "peak_queue_size = " << stats.peak_queue_size << '\n';
    std::cout << "queue_usage_rate = " << stats.queue_usage_rate << '\n';
    std::cout << "peak_queue_usage_rate = " << stats.peak_queue_usage_rate << '\n';
    std::cout << "average_task_time_ms = " << stats.average_task_time_ms << '\n';
    std::cout << "=====================================\n";
}

tinyimx::ThreadPoolOptions MakeOptionsFromConfig(
    const tinyimx::Config& config,
    const std::string& name
) {
    tinyimx::ThreadPoolOptions options =
        tinyimx::ThreadPoolOptions::FromConfig(config.ThreadPool());

    options.name = name;
    return options;
}

void TestBasicTasks(const tinyimx::Config& config) {
    LOG_INFO("TestBasicTasks started");

    tinyimx::ThreadPoolOptions options =
        MakeOptionsFromConfig(config, "basic-task-pool");

    options.queue_full_policy = tinyimx::QueueFullPolicy::kBlock;

    tinyimx::ThreadPool pool(options);
    pool.Start();

    std::vector<std::future<int>> futures;

    for (int i = 0; i < 20; ++i) {
        futures.push_back(pool.Submit([i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            LOG_INFO("basic task running, task_id=" << i);
            return i * i;
        }));
    }

    int sum = 0;
    for (auto& future : futures) {
        sum += future.get();
    }

    LOG_INFO("TestBasicTasks result sum=" << sum);

    pool.Shutdown(tinyimx::ShutdownMode::kGraceful);
    PrintStats("Basic Tasks", pool.GetStats());
}

void TestExceptionTasks(const tinyimx::Config& config) {
    LOG_INFO("TestExceptionTasks started");

    tinyimx::ThreadPoolOptions options =
        MakeOptionsFromConfig(config, "exception-task-pool");

    tinyimx::ThreadPool pool(options);
    pool.Start();

    auto ok_future = pool.Submit([]() {
        return 100;
    });

    auto bad_future = pool.Submit([]() -> int {
        throw std::runtime_error("intentional demo exception");
    });

    try {
        /*
                std::cout << "[ThreadPoolDemo] ok_future = "
                  << ok_future.get() << std::endl;
        */
        LOG_INFO("ok_future=" << ok_future.get());

    } catch (const std::exception& e) {
        std::cout << "[ThreadPoolDemo] unexpected ok_future exception: "
                  << e.what() << std::endl;
    }

    try {
        int value = bad_future.get();
        std::cout << "[ThreadPoolDemo] unexpected bad_future value: "
                  << value << std::endl;
    } catch (const std::exception& e) {
        std::cout << "[ThreadPoolDemo] caught expected exception: "
                  << e.what() << std::endl;
    }

    pool.Shutdown(tinyimx::ShutdownMode::kGraceful);
    PrintStats("Exception Tasks", pool.GetStats());
}

void TestPauseResume(const tinyimx::Config& config) {
    LOG_INFO("TestPauseResume started");

    tinyimx::ThreadPoolOptions options =
        MakeOptionsFromConfig(config, "pause-resume-pool");

    options.worker_threads = 2;
    options.queue_capacity = 16;
    options.queue_full_policy = tinyimx::QueueFullPolicy::kBlock;

    tinyimx::ThreadPool pool(options);
    pool.Start();

    pool.Pause();

    std::atomic<int> counter{0};

    for (int i = 0; i < 6; ++i) {
        pool.Submit([&counter, i]() {
            LOG_INFO("pause/resume task executed, task_id=" << i);
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "[ThreadPoolDemo] counter while paused = "
              << counter.load(std::memory_order_relaxed) << std::endl;

    pool.Resume();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    std::cout << "[ThreadPoolDemo] counter after resume = "
              << counter.load(std::memory_order_relaxed) << std::endl;

    pool.Shutdown(tinyimx::ShutdownMode::kGraceful);
    PrintStats("Pause Resume", pool.GetStats());
}

void TestDiscardPolicy(const tinyimx::Config& config) {
    LOG_INFO("TestDiscardPolicy started");

    tinyimx::ThreadPoolOptions options =
        MakeOptionsFromConfig(config, "discard-policy-pool");

    options.worker_threads = 1;
    options.queue_capacity = 2;
    options.queue_full_policy = tinyimx::QueueFullPolicy::kDiscard;

    tinyimx::ThreadPool pool(options);
    pool.Start();

    std::promise<void> release_worker_promise;
    std::shared_future<void> release_worker_future =
        release_worker_promise.get_future().share();

    pool.Submit([release_worker_future]() {
        LOG_INFO("discard test blocking task started");
        release_worker_future.wait();
        LOG_INFO("discard test blocking task released");
    });

    int rejected_count = 0;

    for (int i = 0; i < 20; ++i) {
        try {
            pool.Submit([i]() {
                LOG_INFO("discard policy task executed, task_id=" << i);
            });
        } catch (const std::exception& e) {
            ++rejected_count;
            LOG_WARN("discard policy task rejected, task_id=" << i
                     << ", reason=" << e.what());
        }
    }

    release_worker_promise.set_value();

    pool.Shutdown(tinyimx::ShutdownMode::kGraceful);

    std::cout << "[ThreadPoolDemo] discard rejected_count = "
              << rejected_count << std::endl;

    PrintStats("Discard Policy", pool.GetStats());
}

void TestOverwritePolicy(const tinyimx::Config& config) {
    LOG_INFO("TestOverwritePolicy started");

    tinyimx::ThreadPoolOptions options =
        MakeOptionsFromConfig(config, "overwrite-policy-pool");

    options.worker_threads = 1;
    options.queue_capacity = 2;
    options.queue_full_policy = tinyimx::QueueFullPolicy::kOverwrite;

    tinyimx::ThreadPool pool(options);
    pool.Start();
    /*
        std::promise<void> release_worker_promise;
        std::shared_future<void> release_worker_future =
            release_worker_promise.get_future().share();

        pool.Submit([release_worker_future]() {
            LOG_INFO("overwrite test blocking task started");
            release_worker_future.wait();
            LOG_INFO("overwrite test blocking task released");
        });

        std::vector<std::future<int>> futures;

        for (int i = 0; i < 20; ++i) {
            try {
                futures.push_back(pool.Submit([i]() {
                    LOG_INFO("overwrite policy task executed, task_id=" << i);
                    return i;
                }));
            } catch (const std::exception& e) {
                LOG_WARN("overwrite policy submit rejected, task_id=" << i
                        << ", reason=" << e.what());
            }
        }

        release_worker_promise.set_value();

    */
    std::promise<void> release_worker_promise;
    std::shared_future<void> release_worker_future = release_worker_promise.get_future().share();

    std::promise<void> blocking_started_promise;
    std::future<void> blocking_started_future = blocking_started_promise.get_future();

    pool.Submit([release_worker_future, &blocking_started_promise]() mutable {
        LOG_INFO("overwrite test blocking task started");
        blocking_started_promise.set_value();
        release_worker_future.wait();
        LOG_INFO("overwrite test blocking task released");
    });

    blocking_started_future.wait();

    std::vector<std::future<int>> futures;

    for (int i = 0; i < 20; ++i) {
        try {
            futures.push_back(pool.Submit([i]() {
                LOG_INFO("overwrite policy task executed, task_id=" << i);
                return i;
            }));
        } catch (const std::exception& e) {
            LOG_WARN("overwrite policy submit rejected, task_id=" << i
                    << ", reason=" << e.what());
        }
    }

    release_worker_promise.set_value();

    int ready_count = 0;
    int broken_count = 0;

    for (auto& future : futures) {
        try {
            future.get();
            ++ready_count;
        } catch (const std::exception&) {
            ++broken_count;
        }
    }

    pool.Shutdown(tinyimx::ShutdownMode::kGraceful);

    std::cout << "[ThreadPoolDemo] overwrite ready_count = "
              << ready_count << std::endl;
    std::cout << "[ThreadPoolDemo] overwrite broken_count = "
              << broken_count << std::endl;

    PrintStats("Overwrite Policy", pool.GetStats());
}

void TestForceShutdown(const tinyimx::Config& config) {
    LOG_INFO("TestForceShutdown started");

    std::promise<void> blocking_started_promise;
    std::future<void> blocking_started_future = blocking_started_promise.get_future();

    tinyimx::ThreadPoolOptions options =
        MakeOptionsFromConfig(config, "force-shutdown-pool");

    options.worker_threads = 1;
    options.queue_capacity = 32;
    options.queue_full_policy = tinyimx::QueueFullPolicy::kBlock;

    tinyimx::ThreadPool pool(options);
    pool.Start();

    pool.Submit([]() {
        LOG_INFO("force shutdown long task started");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        LOG_INFO("force shutdown long task finished");
    });

    for (int i = 0; i < 10; ++i) {
        pool.Submit([i]() {
            LOG_INFO("force shutdown queued task, task_id=" << i);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    pool.Shutdown(tinyimx::ShutdownMode::kForce);
    PrintStats("Force Shutdown", pool.GetStats());
}


void TestDynamicResize(const tinyimx::Config& config) {
    LOG_INFO("TestDynamicResize started");

    tinyimx::ThreadPoolOptions options =
        MakeOptionsFromConfig(config, "dynamic-resize-pool");

    options.enable_dynamic_resize = true;
    options.worker_threads = 2;
    options.min_threads = 2;
    options.max_threads = 6;
    options.queue_capacity = 16;
    options.queue_full_policy = tinyimx::QueueFullPolicy::kBlock;
    options.scale_up_threshold = 0.5;
    options.scale_down_threshold = 0.1;
    options.manager_check_interval = std::chrono::milliseconds(100);
    options.scale_cooldown = std::chrono::milliseconds(200);
    options.worker_idle_timeout = std::chrono::milliseconds(200);

    tinyimx::ThreadPool pool(options);
    pool.Start();

    std::vector<std::future<void>> futures;

    for (int i = 0; i < 80; ++i) {
        futures.push_back(pool.Submit([i]() {
            LOG_INFO("dynamic resize task running, task_id=" << i);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    PrintStats("Dynamic Resize Mid", pool.GetStats());

    for (auto& future : futures) {
        future.get();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    PrintStats("Dynamic Resize After Idle", pool.GetStats());

    pool.Shutdown(tinyimx::ShutdownMode::kGraceful);
    PrintStats("Dynamic Resize Final", pool.GetStats());
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway.json";

    if (argc >= 2) {
        config_path = argv[1];
    }

    tinyimx::Config config;
    if (!config.LoadFromFile(config_path)) {
        std::cerr << "[ThreadPoolDemo] load config failed: "
                  << config.LastError() << std::endl;
        return 1;
    }

    if (!tinyimx::Logger::Instance().Init(config.Logger())) {
        std::cerr << "[ThreadPoolDemo] logger init failed" << std::endl;
        return 1;
    }

    LOG_INFO("thread pool demo started");

    try {
        TestBasicTasks(config);
        TestExceptionTasks(config);
        TestPauseResume(config);
        TestDiscardPolicy(config);
        TestOverwritePolicy(config);
        TestForceShutdown(config);
        TestDynamicResize(config);
    } catch (const std::exception& e) {
        LOG_ERROR("thread pool demo failed, error=" << e.what());
        tinyimx::Logger::Instance().Shutdown();
        return 1;
    }

    LOG_INFO("thread pool demo finished");

    tinyimx::Logger::Instance().Flush();
    tinyimx::Logger::Instance().Shutdown();

    return 0;

}