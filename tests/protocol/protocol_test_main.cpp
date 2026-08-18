#include "tests/concurrency/TestFramework.h"

namespace tinyimx::test {

void RegisterProtocolCodecTests(TestRunner& runner);
void RegisterGatewayPeerProtocolTests(TestRunner& runner);
void RegisterClientChatProtocolTests(TestRunner& runner);

}  // namespace tinyimx::test

int main() {
    tinyimx::test::TestRunner runner;

    tinyimx::test::RegisterProtocolCodecTests(runner);
    tinyimx::test::RegisterGatewayPeerProtocolTests(runner);
    tinyimx::test::RegisterClientChatProtocolTests(runner);
    const int failed_count = runner.RunAll("TinyIMX Protocol Tests");

    return failed_count == 0 ? 0 : 1;
}
