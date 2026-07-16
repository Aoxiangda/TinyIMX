#include "tests/concurrency/TestFramework.h"

#include "common/net/InetAddress.h"

namespace tinyimx::test {
    void RegisterInetAddressTests(TestRunner& runner) {
        runner.Add("InetAddress.ValidAddress", []() {
            InetAddress address("127.0.0.1", 9000);

            TINYIMX_EXPECT_TRUE(address.IsValid());
            TINYIMX_EXPECT_EQ(address.Ip(), std::string("127.0.0.1"));
            TINYIMX_EXPECT_EQ(address.Port(), static_cast<uint16_t>(9000));
            TINYIMX_EXPECT_EQ(address.ToString(), std::string("127.0.0.1:9000"));
        });

        runner.Add("InetAddress.AnyAddress", []() {
            InetAddress address("0.0.0.0", 9000);

            TINYIMX_EXPECT_TRUE(address.IsValid());
            TINYIMX_EXPECT_EQ(address.Ip(), std::string("0.0.0.0"));
            TINYIMX_EXPECT_EQ(address.Port(), static_cast<uint16_t>(9000));
        });

        runner.Add("InetAddress.InvalidAddress", []() {
            InetAddress address("invalid-ip", 9000);

            TINYIMX_EXPECT_TRUE(!address.IsValid());
        });
    }
} // namespace tinyimx::test
