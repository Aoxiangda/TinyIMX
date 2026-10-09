#include "common/logging/LogMacros.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <sys/resource.h>

namespace {
unsigned checks = 0;
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    ++checks;
}
void EmitEnabledRecord(int& evaluations) {
    LOG_INFO("enabled-source-record=" << ++evaluations);
}
std::string ReadFile(const std::string& path) {
    std::ifstream file(path);
    return std::string(std::istreambuf_iterator<char>(file), {});
}
double CPUSeconds() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) throw std::runtime_error("getrusage");
    return usage.ru_utime.tv_sec + usage.ru_stime.tv_sec +
           (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1e6;
}
void EagerDiscardedRecord(const std::string& payload, std::size_t index) {
    // Exact former macro order, using the same singleton and filter counter.
    std::ostringstream stream;
    stream << "connection=test-peer, bytes=" << index << ", payload=" << payload;
    tinyimx::Logger::Instance().Log(tinyimx::LogLevel::kDebug,
        tinyimx::SourceLocation{__FILE__, __LINE__, __func__}, stream.str());
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 2 || std::string(argv[1]) != "owned-regression") return 2;
        const std::string file = "/opt/codex-output/lazy.log";
        Check(std::filesystem::is_directory("/opt/codex-output") &&
              !std::filesystem::exists(file), "fresh own log only");
        auto& logger = tinyimx::Logger::Instance();
        const auto init = [&](const std::string& level) {
            Check(logger.Init(level, file, false), "logger init");
        };
        init("info");
        int evaluations = 0;
        LOG_TRACE("discarded=" << ++evaluations);
        LOG_DEBUG("discarded=" << ++evaluations);
        Check(evaluations == 0 && logger.FilteredLogCount() == 2 &&
              logger.WrittenLogCount() == 0 && logger.FailedLogCount() == 0,
              "filtered factories and counters");
        EmitEnabledRecord(evaluations);
        Check(evaluations == 1 && logger.WrittenLogCount() == 1,
              "enabled expression exactly once");
        logger.Flush();
        auto contents = ReadFile(file);
        Check(contents.find("enabled-source-record=1") != std::string::npos &&
              contents.find("EmitEnabledRecord") != std::string::npos &&
              contents.find("discarded=") == std::string::npos,
              "enabled contents, original function and discarded absence");

        logger.Log(tinyimx::LogLevel::kDebug, __FILE__, __LINE__, "direct-discarded");
        logger.Log(tinyimx::LogLevel::kInfo, __FILE__, __LINE__, "direct-enabled");
        Check(logger.FilteredLogCount() == 3 && logger.WrittenLogCount() == 2,
              "direct API retained");
        const auto throwing = []() -> std::string { throw std::runtime_error("factory"); };
        logger.LogLazy(tinyimx::LogLevel::kDebug, {__FILE__, __LINE__, __func__}, throwing);
        Check(logger.FilteredLogCount() == 4, "disabled throwing factory skipped");
        bool threw = false;
        try { logger.LogLazy(tinyimx::LogLevel::kInfo,
                    {__FILE__, __LINE__, __func__}, throwing); }
        catch (const std::runtime_error&) { threw = true; }
        Check(threw && logger.WrittenLogCount() == 2 && logger.FilteredLogCount() == 4,
              "enabled factory exception retains former behavior");

        unsigned level_calls = 0;
        const auto choose_level = [&]() { ++level_calls; return tinyimx::LogLevel::kInfo; };
        if (true) TINYIMX_LOG(choose_level(), "level-once");
        else throw std::runtime_error("macro control flow");
        Check(level_calls == 1 && logger.WrittenLogCount() == 3,
              "level evaluation and if else");
        std::promise<int> promise;
        auto future = promise.get_future();
        promise.set_value(100);
        const int result = future.get();
        LOG_DEBUG("future=" << result);
        Check(result == 100 && !future.valid() && logger.FilteredLogCount() == 5,
              "future consumption outside filtered log");

        init("trace");
        LOG_TRACE("severity-trace"); LOG_DEBUG("severity-debug");
        LOG_INFO("severity-info"); LOG_WARN("severity-warn");
        LOG_ERROR("severity-error"); LOG_FATAL("severity-fatal");
        TINYIMX_LOG(tinyimx::LogLevel::kOff, "off=" << ++evaluations);
        Check(logger.WrittenLogCount() == 6 && logger.FilteredLogCount() == 1 &&
              logger.FailedLogCount() == 0 && evaluations == 1, "all severities and off");
        // Fatal must have flushed its record before the caller asks for Flush.
        Check(ReadFile(file).find("severity-fatal") != std::string::npos, "fatal flush");
        init("off");
        LOG_FATAL("off-fatal=" << ++evaluations);
        Check(logger.FilteredLogCount() == 1 && evaluations == 1, "off threshold");

        init("info");
        logger.LogLazy(tinyimx::LogLevel::kInfo, {__FILE__, __LINE__, __func__}, [&]() {
            init("error");
            return std::string("must-not-write-after-threshold-change");
        });
        Check(logger.WrittenLogCount() == 0 && logger.FilteredLogCount() == 1,
              "final level recheck after factory");
        logger.Flush();
        Check(ReadFile(file).find("must-not-write-after-threshold-change") == std::string::npos,
              "no empty or stale record on level change");

        init("info");
        std::atomic<unsigned> concurrent_evaluations{0};
        std::vector<std::thread> threads;
        for (int worker = 0; worker < 8; ++worker) threads.emplace_back([&]() {
            for (int i = 0; i < 1000; ++i)
                LOG_DEBUG("concurrent=" << concurrent_evaluations.fetch_add(1));
        });
        for (auto& thread : threads) thread.join();
        Check(concurrent_evaluations == 0 && logger.FilteredLogCount() == 8000 &&
              logger.WrittenLogCount() == 0, "concurrent filtering exact count");
        logger.Shutdown();
        LOG_INFO("uninitialized=" << ++evaluations);
        LOG_DEBUG("uninitialized-discarded=" << ++evaluations);
        Check(evaluations == 2 && logger.FailedLogCount() == 1 &&
              logger.FilteredLogCount() == 8001, "uninitialized API semantics");

        init("info");
        constexpr std::size_t count = 100000;
        std::cout << "{\"status\":\"LAZY_LOGGING_REGRESSION_PASS\",\"checks\":" << checks
                  << ",\"records_per_case\":" << count << ",\"cases\":[";
        unsigned case_number = 0;
        for (unsigned payload_bytes : {0U, 512U}) {
          const std::string payload(payload_bytes, 'x');
          for (bool lazy : {false, true, true, false}) {
            const auto filtered_before = logger.FilteredLogCount();
            const double cpu_before = CPUSeconds();
            const auto wall_before = std::chrono::steady_clock::now();
            for (std::size_t i = 0; i < count; ++i) {
                if (lazy) LOG_DEBUG("connection=test-peer, bytes=" << i << ", payload=" << payload);
                else EagerDiscardedRecord(payload, i);
            }
            const double wall = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - wall_before).count();
            const double cpu = CPUSeconds() - cpu_before;
            const auto discarded = logger.FilteredLogCount() - filtered_before;
            Check(discarded == count && logger.WrittenLogCount() == 0 &&
                  logger.FailedLogCount() == 0, "ABBA discards and no writes");
            if (case_number++) std::cout << ',';
            std::cout << "{\"mode\":\"" << (lazy ? "lazy" : "eager")
                      << "\",\"payload_bytes\":" << payload_bytes
                      << ",\"filtered\":" << discarded << ",\"cpu_seconds\":" << cpu
                      << ",\"wall_seconds\":" << wall << '}';
          }
        }
        logger.Shutdown();
        std::cout << "],\"checks_after_performance\":" << checks
                  << ",\"full_feature_acceptance\":false}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
