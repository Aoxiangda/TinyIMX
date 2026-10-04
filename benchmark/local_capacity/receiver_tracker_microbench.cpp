#include "gateway/ReceiverDeliveryTracker.h"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <sys/resource.h>

// Isolated data-structure experiment; record counts are NOT concurrent users.
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const auto n = std::strtoull(argv[1], nullptr, 10);
    if (n != 10000 && n != 50000) return 2;
    using Clock = std::chrono::steady_clock;
    auto elapsed = [](Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    };
    tinyimx::ReceiverDeliveryTracker tracker;
    std::uint64_t checksum = 0;
    auto start = Clock::now();
    for (std::uint64_t m = 1; m <= n; ++m) {
        if (tracker.RegisterAttempt(m, 100000 + m % 1000, static_cast<std::uint32_t>(m)) !=
            tinyimx::ReceiverDeliveryRegisterStatus::kRegistered) return 1;
    }
    const auto registration_ms = elapsed(start);
    constexpr std::uint64_t probes = 5000;
    start = Clock::now();
    for (std::uint64_t i = 0; i < probes; ++i) {
        const auto m = 1 + (i * 7919) % n;
        tinyimx::ReceiverDeliverySnapshot snapshot;
        if (!tracker.GetSnapshot(m, &snapshot) || snapshot.confirmed) return 1;
        checksum += snapshot.message_id;
    }
    const auto snapshot_ms = elapsed(start);
    start = Clock::now();
    for (std::uint64_t i = 0; i < probes; ++i) {
        const auto m = 1 + (i * 7919) % n;
        if (tracker.Acknowledge(m, 999999, static_cast<std::uint32_t>(m)) !=
            tinyimx::ReceiverDeliveryAckStatus::kReceiverMismatch) return 1;
    }
    const auto mismatch_ms = elapsed(start);
    start = Clock::now();
    for (std::uint64_t m = 1; m <= n; ++m) {
        if (tracker.Acknowledge(m, 100000 + m % 1000, static_cast<std::uint32_t>(m)) !=
            tinyimx::ReceiverDeliveryAckStatus::kConfirmed) return 1;
    }
    const auto ack_ms = elapsed(start);
    struct rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    std::cout << "{\"experiment\":\"tracker_record_microbenchmark_not_user_capacity\","
              << "\"records\":" << n << ",\"probe_operations\":" << probes
              << ",\"registration_ms\":" << registration_ms
              << ",\"snapshot_ms\":" << snapshot_ms << ",\"mismatch_ms\":" << mismatch_ms
              << ",\"ack_ms\":" << ack_ms << ",\"max_rss_kib\":" << usage.ru_maxrss
              << ",\"checksum\":" << checksum << ",\"remaining_entries\":" << tracker.Size()
              << ",\"confirmed_cache\":" << tracker.ConfirmedCount() << ",\"status\":\"PASS\"}\n";
    return tracker.Size() == 10000 && tracker.ConfirmedCount() == 10000 ? 0 : 1;
}
