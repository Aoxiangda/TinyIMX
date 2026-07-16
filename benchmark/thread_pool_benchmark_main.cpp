#include "benchmark/thread_pool_benchmark.h"

#include "common/config/Config.h"
#include "common/logging/Logger.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

tinyimx::BenchmarkMode ParseMode(const std::string& mode) {
    if (mode == "fixed") {
        return tinyimx::BenchmarkMode::kFixedCount;
    }

    if (mode == "queue") {
        return tinyimx::BenchmarkMode::kQueuePolicy;
    }

    if (mode == "dynamic") {
        return tinyimx::BenchmarkMode::kDynamicResize;
    }

    return tinyimx::BenchmarkMode::kFixedCount;
}

tinyimx::QueueFullPolicy ParsePolicy(const std::string& policy) {
    if (policy == "discard") {
        return tinyimx::QueueFullPolicy::kDiscard;
    }

    if (policy == "overwrite") {
        return tinyimx::QueueFullPolicy::kOverwrite;
    }

    return tinyimx::QueueFullPolicy::kBlock;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway.json";
    std::string csv_path = "benchmark/results/thread_pool_benchmark.csv";

    tinyimx::ThreadPoolBenchmarkConfig benchmark_config;

    if (argc >= 2) {
        benchmark_config.mode = ParseMode(argv[1]);
    }

    if (argc >= 3) {
        benchmark_config.total_tasks =
            static_cast<std::size_t>(std::stoull(argv[2]));
    }

    if (argc >= 4) {
        benchmark_config.worker_threads =
            static_cast<std::size_t>(std::stoull(argv[3]));
    }

    if (argc >= 5) {
        benchmark_config.queue_capacity =
            static_cast<std::size_t>(std::stoull(argv[4]));
    }

    if (argc >= 6) {
        benchmark_config.queue_full_policy = ParsePolicy(argv[5]);
    }


    if (argc >= 7) {
        csv_path = argv[6];
    }

    tinyimx::Config config;
    if (!config.LoadFromFile(config_path)) {
        std::cerr << "[ThreadPoolBenchmark] load config failed: "
                  << config.LastError() << std::endl;
        return 1;
    }

    if (!tinyimx::Logger::Instance().Init(config.Logger())) {
        std::cerr << "[ThreadPoolBenchmark] logger init failed" << std::endl;
        return 1;
    }

    const auto& thread_pool_config = config.ThreadPool();

    benchmark_config.min_threads = thread_pool_config.min_threads;
    benchmark_config.max_threads = thread_pool_config.max_threads;
    benchmark_config.scale_up_threshold =
        thread_pool_config.scale_up_threshold;
    benchmark_config.scale_down_threshold =
        thread_pool_config.scale_down_threshold;
    benchmark_config.manager_check_interval =
        std::chrono::milliseconds(
            thread_pool_config.manager_thread_interval_ms
        );
    benchmark_config.scale_cooldown =
        std::chrono::milliseconds(
            thread_pool_config.scale_cooldown_ms
        );
    benchmark_config.worker_idle_timeout =
        std::chrono::milliseconds(
            thread_pool_config.worker_idle_timeout_ms
        );

    if (benchmark_config.mode == tinyimx::BenchmarkMode::kDynamicResize) {
        benchmark_config.enable_dynamic_resize = true;
        benchmark_config.worker_threads = 2;
        benchmark_config.min_threads = 2;
        benchmark_config.max_threads = 8;
        benchmark_config.queue_capacity = 64;
        benchmark_config.task_sleep = std::chrono::milliseconds(20);
        benchmark_config.manager_check_interval =
            std::chrono::milliseconds(100);
        benchmark_config.scale_cooldown =
            std::chrono::milliseconds(200);
        benchmark_config.worker_idle_timeout =
            std::chrono::milliseconds(200);
    }

    if (benchmark_config.mode == tinyimx::BenchmarkMode::kQueuePolicy) {
        benchmark_config.worker_threads = 1;
        benchmark_config.queue_capacity =
            benchmark_config.queue_capacity == 1024
                ? 16
                : benchmark_config.queue_capacity;
        benchmark_config.task_sleep = std::chrono::milliseconds(5);
    }

    tinyimx::ThreadPoolBenchmark benchmark(benchmark_config);
    const auto result = benchmark.Run();

    tinyimx::ThreadPoolBenchmark::PrintResult(result);


    const bool csv_ok = tinyimx::ThreadPoolBenchmark::AppendCsvResult(result, csv_path);
    if (!csv_ok) {
        std::cerr << "{ThreadPoolBenchmark} appened csv result failed"
            << csv_path <<std::endl;
    } else {
        std::cout << "{ThreadPoolBenchmark} appened csv result success: "
            << csv_path << std::endl;
    }
    tinyimx::Logger::Instance().Flush();
    tinyimx::Logger::Instance().Shutdown();

    return 0;
}