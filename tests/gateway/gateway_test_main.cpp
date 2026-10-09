#include "tests/concurrency/TestFramework.h"

#include "common/config/ConfigTypes.h"
#include "common/logging/Logger.h"

#include <iostream>

namespace tinyimx::test {

void RegisterOfflineMessageStoreTests(TestRunner& runner);

void RegisterSessionManagerTests(TestRunner& runner);

void RegisterGatewayPeerTransportTests(TestRunner& runner);

void RegisterMessageDeliveryDeduplicatorTests(TestRunner& runner);

void RegisterReceiverDeliveryTrackerTests(TestRunner& runner);

void RegisterGroupFanoutCoordinatorTests(TestRunner& runner);

void RegisterGroupPeerDeliveryTests(TestRunner& runner);
}  // namespace tinyimx::test

int main() {
    tinyimx::LoggerConfig logger_config;
    logger_config.level = "error";
    logger_config.console = true;
    logger_config.file = "logs/gateway_tests.log";
    logger_config.max_file_size_mb = 10;
    logger_config.max_backup_files = 2;
    logger_config.flush_each_log = false;

    if (!tinyimx::Logger::Instance().Init(logger_config)) {
        std::cerr << "[GatewayTests] logger init failed\n";
        return 1;
    }

    tinyimx::test::TestRunner runner;

    tinyimx::test::RegisterOfflineMessageStoreTests(runner);
    tinyimx::test::RegisterSessionManagerTests(runner);
    tinyimx::test::RegisterGatewayPeerTransportTests(runner);
    tinyimx::test::RegisterMessageDeliveryDeduplicatorTests(runner);
    tinyimx::test::RegisterReceiverDeliveryTrackerTests(runner);
    tinyimx::test::RegisterGroupFanoutCoordinatorTests(runner);
    tinyimx::test::RegisterGroupPeerDeliveryTests(runner);
    const int failed_count = runner.RunAll("TinyIMX Gateway Tests");

    tinyimx::Logger::Instance().Flush();
    tinyimx::Logger::Instance().Shutdown();

    return failed_count == 0 ? 0 : 1;
}