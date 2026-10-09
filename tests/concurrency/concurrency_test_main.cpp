#include "tests/concurrency/TestFramework.h"

#include "common/config/ConfigTypes.h"
#include "common/logging/Logger.h"

#include <iostream>

namespace tinyimx::test {

void RegisterCircularQueueTests(TestRunner& runner);
void RegisterMpmcBlockingQueueTests(TestRunner& runner);
void RegisterThreadPoolTests(TestRunner& runner);

}  // namespace tinyimx::test

int main() {
    tinyimx::LoggerConfig logger_config;
    logger_config.level = "error";
    logger_config.console = true;
    logger_config.file = "logs/concurrency_tests.log";
    logger_config.max_file_size_mb = 10;
    logger_config.max_backup_files = 2;
    logger_config.flush_each_log = false;

    if (!tinyimx::Logger::Instance().Init(logger_config)) {
        std::cerr << "[ConcurrencyTests] logger init failed\n";
        return 1;
    }

    tinyimx::test::TestRunner runner;

    tinyimx::test::RegisterCircularQueueTests(runner);
    tinyimx::test::RegisterMpmcBlockingQueueTests(runner);
    tinyimx::test::RegisterThreadPoolTests(runner);

    const int failed_count = runner.RunAll("TinyIMX Concurrency Tests");

    tinyimx::Logger::Instance().Flush();
    tinyimx::Logger::Instance().Shutdown();

    return failed_count == 0 ? 0 : 1;
}