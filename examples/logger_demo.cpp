#include "common/config/Config.h"
#include "common/logging/LogMacros.h"
#include "common/logging/Logger.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main(int argc, char* argv[]) {
    std::string config_path = "config/gateway.json";

    if (argc >= 2) {
        config_path = argv[1];
    }

    tinyimx::Config config;
    if (!config.LoadFromFile(config_path)) {
        std::cerr << "[LoggerDemo] load config failed: "
                  << config.LastError() << std::endl;
        return 1;
    }

    if (!tinyimx::Logger::Instance().Init(config.Logger())) {
        std::cerr << "[LoggerDemo] logger init failed" << std::endl;
        return 1;
    }

    LOG_TRACE("trace log, may be filtered by config level");
    LOG_DEBUG("debug log, may be filtered by config level");
    LOG_INFO("logger demo started");
    LOG_WARN("this is a warning log");
    LOG_ERROR("this is an error log");

    std::vector<std::thread> threads;

    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([i]() {
            for (int j = 0; j < 10; ++j) {
                LOG_INFO("multi-thread log"
                         << ", thread_index=" << i
                         << ", log_index=" << j);

                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        });
    }

    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    LOG_FATAL("fatal level log demo, program will not exit automatically");

    LOG_INFO("start rolling log test");

    for (int i = 0; i < 5000; ++i) {
        LOG_INFO("rolling test log"
                << ", index=" << i
                << ", payload=abcdefghijklmnopqrstuvwxyz"
                << "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                << "0123456789");
    }

    LOG_INFO("rolling log test finished");

    tinyimx::Logger::Instance().Flush();

    std::cout << "[LoggerDemo] written="
              << tinyimx::Logger::Instance().WrittenLogCount()
              << ", filtered="
              << tinyimx::Logger::Instance().FilteredLogCount()
              << ", failed="
              << tinyimx::Logger::Instance().FailedLogCount()
              << std::endl;

    tinyimx::Logger::Instance().Shutdown();
    return 0;
}