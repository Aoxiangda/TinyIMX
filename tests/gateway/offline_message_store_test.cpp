#include "tests/concurrency/TestFramework.h"

#include "gateway/OfflineMessageStore.h"

#include <cstdint>
#include <string>

namespace tinyimx::test {
namespace {

Packet MakeChatPacket(std::uint32_t seq,
                      const std::string& text) {
    Packet packet;
    packet.type = MessageType::kChatMessage;
    packet.seq = seq;
    packet.body =
        std::string(R"({"from":10001,"to":10002,"text":")") +
        text +
        R"("})";

    return packet;
}

}  // namespace

void RegisterOfflineMessageStoreTests(TestRunner& runner) {
    runner.Add("OfflineMessageStore.StoreAndPopAll", []() {
        OfflineMessageStore store(10);

        TINYIMX_EXPECT_TRUE(
            store.Store(10002, MakeChatPacket(1, "hello-1"))
        );

        TINYIMX_EXPECT_TRUE(
            store.Store(10002, MakeChatPacket(2, "hello-2"))
        );

        TINYIMX_EXPECT_EQ(
            store.PendingCount(10002),
            static_cast<std::size_t>(2)
        );

        TINYIMX_EXPECT_EQ(
            store.TotalCount(),
            static_cast<std::size_t>(2)
        );

        const auto packets = store.PopAll(10002);

        TINYIMX_EXPECT_EQ(
            packets.size(),
            static_cast<std::size_t>(2)
        );

        TINYIMX_EXPECT_EQ(
            packets[0].seq,
            static_cast<std::uint32_t>(1)
        );

        TINYIMX_EXPECT_EQ(
            packets[1].seq,
            static_cast<std::uint32_t>(2)
        );

        TINYIMX_EXPECT_EQ(
            store.PendingCount(10002),
            static_cast<std::size_t>(0)
        );

        TINYIMX_EXPECT_EQ(
            store.TotalCount(),
            static_cast<std::size_t>(0)
        );
    });

    runner.Add("OfflineMessageStore.PopUnknownUser", []() {
        OfflineMessageStore store(10);

        const auto packets = store.PopAll(99999);

        TINYIMX_EXPECT_TRUE(packets.empty());
        TINYIMX_EXPECT_EQ(
            store.PendingCount(99999),
            static_cast<std::size_t>(0)
        );
        TINYIMX_EXPECT_EQ(
            store.TotalCount(),
            static_cast<std::size_t>(0)
        );
    });

    runner.Add("OfflineMessageStore.OverflowDropsOldest", []() {
        OfflineMessageStore store(2);

        TINYIMX_EXPECT_TRUE(
            store.Store(10002, MakeChatPacket(1, "oldest"))
        );

        TINYIMX_EXPECT_TRUE(
            store.Store(10002, MakeChatPacket(2, "middle"))
        );

        TINYIMX_EXPECT_TRUE(
            store.Store(10002, MakeChatPacket(3, "newest"))
        );

        TINYIMX_EXPECT_EQ(
            store.PendingCount(10002),
            static_cast<std::size_t>(2)
        );

        const auto packets = store.PopAll(10002);

        TINYIMX_EXPECT_EQ(
            packets.size(),
            static_cast<std::size_t>(2)
        );

        TINYIMX_EXPECT_EQ(
            packets[0].seq,
            static_cast<std::uint32_t>(2)
        );

        TINYIMX_EXPECT_EQ(
            packets[1].seq,
            static_cast<std::uint32_t>(3)
        );
    });

    runner.Add("OfflineMessageStore.RejectZeroUserId", []() {
        OfflineMessageStore store(10);

        TINYIMX_EXPECT_TRUE(
            !store.Store(0, MakeChatPacket(1, "invalid"))
        );

        TINYIMX_EXPECT_EQ(
            store.TotalCount(),
            static_cast<std::size_t>(0)
        );
    });
}

}  // namespace tinyimx::test