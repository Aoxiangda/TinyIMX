#include "tests/concurrency/TestFramework.h"

#include "common/config/ConfigTypes.h"
#include "common/logging/Logger.h"

#include <iostream>

namespace tinyimx::test {

void RegisterInetAddressTests(TestRunner& runner);
void RegisterBufferTests(TestRunner& runner);
void RegisterEventLoopTests(TestRunner& runner);
void RegisterTcpServerTests(TestRunner& runner);

}  // namespace tinyimx::test

int main() {
    tinyimx::LoggerConfig logger_config;
    logger_config.level = "error";
    logger_config.console = true;
    logger_config.file = "logs/net_tests.log";
    logger_config.max_file_size_mb = 10;
    logger_config.max_backup_files = 2;
    logger_config.flush_each_log = false;

    if (!tinyimx::Logger::Instance().Init(logger_config)) {
        std::cerr << "[NetTests] logger init failed\n";
        return 1;
    }

    tinyimx::test::TestRunner runner;

    tinyimx::test::RegisterInetAddressTests(runner);
    tinyimx::test::RegisterBufferTests(runner);
    tinyimx::test::RegisterEventLoopTests(runner);
    tinyimx::test::RegisterTcpServerTests(runner);

    const int failed_count = runner.RunAll("TinyIMX Network Tests");

    tinyimx::Logger::Instance().Flush();
    tinyimx::Logger::Instance().Shutdown();

    return failed_count == 0 ? 0 : 1;
}