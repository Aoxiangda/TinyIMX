#include "tests/concurrency/TestFramework.h"

#include "gateway/ReceiverDeliveryTracker.h"
#include <atomic>
#include <thread>
#include <vector>


namespace tinyimx::test {


void
RegisterReceiverDeliveryTrackerTests(
    TestRunner& runner
) {
    runner.Add("ReceiverDeliveryTracker.WindowCountBytesAndAckRelease", []() {
        ReceiverDeliveryTracker::WindowOptions limits;
        limits.max_waiting_entries = 3;
        limits.max_waiting_per_recipient = 2;
        limits.max_waiting_bytes = 100;
        limits.max_bytes_per_recipient = 60;
        ReceiverDeliveryTracker tracker(10, limits);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(PrivateDeliveryIdentity(1, 10), 1, 30), ReceiverDeliveryRegisterStatus::kRegistered);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(PrivateDeliveryIdentity(2, 10), 2, 30), ReceiverDeliveryRegisterStatus::kRegistered);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(PrivateDeliveryIdentity(3, 10), 3, 1), ReceiverDeliveryRegisterStatus::kWindowFull);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(PrivateDeliveryIdentity(4, 20), 4, 40), ReceiverDeliveryRegisterStatus::kRegistered);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(PrivateDeliveryIdentity(5, 30), 5, 1), ReceiverDeliveryRegisterStatus::kWindowFull);
        auto state = tracker.GetWindowStats();
        TINYIMX_EXPECT_EQ(state.waiting_entries, static_cast<std::size_t>(3));
        TINYIMX_EXPECT_EQ(state.waiting_bytes, static_cast<std::size_t>(100));
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(1, 10, 999), ReceiverDeliveryAckStatus::kUnknownAttempt);
        TINYIMX_EXPECT_EQ(tracker.GetWindowStats().waiting_bytes, static_cast<std::size_t>(100));
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(1, 10, 1), ReceiverDeliveryAckStatus::kConfirmed);
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(1, 10, 1), ReceiverDeliveryAckStatus::kDuplicate);
        TINYIMX_EXPECT_EQ(tracker.GetWindowStats().waiting_bytes, static_cast<std::size_t>(70));
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(PrivateDeliveryIdentity(5, 30), 5, 30), ReceiverDeliveryRegisterStatus::kRegistered);
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(2, 10, 2), ReceiverDeliveryAckStatus::kConfirmed);
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(4, 20, 4), ReceiverDeliveryAckStatus::kConfirmed);
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(5, 30, 5), ReceiverDeliveryAckStatus::kConfirmed);
        state = tracker.GetWindowStats();
        TINYIMX_EXPECT_EQ(state.waiting_entries, static_cast<std::size_t>(0));
        TINYIMX_EXPECT_EQ(state.waiting_bytes, static_cast<std::size_t>(0));
        TINYIMX_EXPECT_EQ(state.waiting_recipients, static_cast<std::size_t>(0));
    });
    runner.Add("ReceiverDeliveryTracker.WindowByteOverflowAndOversizeRejectBeforeMutation", []() {
        ReceiverDeliveryTracker::WindowOptions limits;
        limits.max_waiting_bytes = 100;
        limits.max_bytes_per_recipient = 60;
        ReceiverDeliveryTracker tracker(10, limits);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(PrivateDeliveryIdentity(1, 10), 1, 61), ReceiverDeliveryRegisterStatus::kWindowFull);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(PrivateDeliveryIdentity(2, 20), 2, static_cast<std::size_t>(-1)), ReceiverDeliveryRegisterStatus::kWindowFull);
        TINYIMX_EXPECT_EQ(tracker.Size(), static_cast<std::size_t>(0));
        TINYIMX_EXPECT_EQ(tracker.GetWindowStats().waiting_bytes, static_cast<std::size_t>(0));
    });
    runner.Add("ReceiverDeliveryTracker.WindowGrowthRejectionPreservesOldAttemptEvidence", []() {
        ReceiverDeliveryTracker::WindowOptions limits;
        limits.max_waiting_bytes = 100;
        limits.max_bytes_per_recipient = 60;
        ReceiverDeliveryTracker tracker(10, limits);
        const auto id = PrivateDeliveryIdentity(1, 10);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(id, 1, 40), ReceiverDeliveryRegisterStatus::kRegistered);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(id, 2, 61), ReceiverDeliveryRegisterStatus::kWindowFull);
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(id, 2), ReceiverDeliveryAckStatus::kUnknownAttempt);
        TINYIMX_EXPECT_EQ(tracker.GetWindowStats().attempt_sequences, static_cast<std::size_t>(1));
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(id, 3, 60), ReceiverDeliveryRegisterStatus::kRetryRegistered);
        TINYIMX_EXPECT_EQ(tracker.GetWindowStats().waiting_bytes, static_cast<std::size_t>(60));
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(id, 1), ReceiverDeliveryAckStatus::kConfirmed);
        TINYIMX_EXPECT_EQ(tracker.GetWindowStats().waiting_bytes, static_cast<std::size_t>(0));
    });
    runner.Add("ReceiverDeliveryTracker.WindowSharesRecipientQuotaAcrossPrivateAndGroup", []() {
        ReceiverDeliveryTracker::WindowOptions limits;
        limits.max_waiting_per_recipient = 2;
        limits.max_waiting_bytes = 100;
        limits.max_bytes_per_recipient = 60;
        ReceiverDeliveryTracker tracker(10, limits);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(PrivateDeliveryIdentity(1, 10), 1, 30), ReceiverDeliveryRegisterStatus::kRegistered);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(GroupDeliveryIdentity(1, 10), 2, 20), ReceiverDeliveryRegisterStatus::kRegistered);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(GroupDeliveryIdentity(2, 10), 3, 1), ReceiverDeliveryRegisterStatus::kWindowFull);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(GroupDeliveryIdentity(1, 20), 4, 25), ReceiverDeliveryRegisterStatus::kRegistered);
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(GroupDeliveryIdentity(1, 10), 2), ReceiverDeliveryAckStatus::kConfirmed);
        TINYIMX_EXPECT_EQ(tracker.GetWindowStats().waiting_bytes, static_cast<std::size_t>(55));
        TINYIMX_EXPECT_EQ(tracker.GetWindowStats().waiting_entries, static_cast<std::size_t>(2));
    });
    runner.Add("ReceiverDeliveryTracker.RecoveryCooldownAndBoundedKnownDRetransmission", []() {
        ReceiverDeliveryTracker::WindowOptions limits;
        limits.max_attempt_sequences = 2;
        ReceiverDeliveryTracker tracker(10, limits);
        const auto id = PrivateDeliveryIdentity(1, 10);
        const auto now = ReceiverDeliveryTracker::Clock::now();
        const auto first = tracker.RegisterReplayAttempt(id, 1, 20, now);
        TINYIMX_EXPECT_EQ(first.status, ReceiverDeliveryRegisterStatus::kRegistered);
        TINYIMX_EXPECT_EQ(tracker.RegisterReplayAttempt(id, 2, 20, now + std::chrono::seconds(1)).status,
                          ReceiverDeliveryRegisterStatus::kWaitingAck);
        const auto retry = tracker.RegisterReplayAttempt(id, 2, 20, now + std::chrono::seconds(6));
        TINYIMX_EXPECT_EQ(retry.status, ReceiverDeliveryRegisterStatus::kRetryRegistered);
        for (int i = 2; i < 100; ++i) {
            const auto retransmission = tracker.RegisterReplayAttempt(id, static_cast<std::uint32_t>(i + 1), 20,
                now + std::chrono::seconds(i * 6));
            TINYIMX_EXPECT_EQ(retransmission.status, ReceiverDeliveryRegisterStatus::kRetryRegistered);
            TINYIMX_EXPECT_TRUE(retransmission.reused_attempt);
            TINYIMX_EXPECT_EQ(retransmission.delivery_seq, static_cast<std::uint32_t>(2));
        }
        TINYIMX_EXPECT_EQ(tracker.GetWindowStats().attempt_sequences, static_cast<std::size_t>(2));
        TINYIMX_EXPECT_EQ(tracker.GetWindowStats().waiting_bytes, static_cast<std::size_t>(20));
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(id, 1), ReceiverDeliveryAckStatus::kConfirmed);
        TINYIMX_EXPECT_EQ(tracker.GetWindowStats().waiting_entries, static_cast<std::size_t>(0));
        TINYIMX_EXPECT_EQ(tracker.RegisterReplayAttempt(id, 999, 20, now + std::chrono::hours(1)).status,
                          ReceiverDeliveryRegisterStatus::kAlreadyConfirmed);
    });
    runner.Add("ReceiverDeliveryTracker.RetryEvidenceCapPreservesEarliestLateAck", []() {
        ReceiverDeliveryTracker::WindowOptions limits;
        limits.max_attempt_sequences = 2;
        ReceiverDeliveryTracker tracker(1, limits);
        const auto id = PrivateDeliveryIdentity(1, 10);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(id, 1, 10), ReceiverDeliveryRegisterStatus::kRegistered);
        TINYIMX_EXPECT_EQ(tracker.RegisterRetryAttempt(id, 1, 2, 1000), ReceiverDeliveryRetryRegisterStatus::kRetryRegistered);
        TINYIMX_EXPECT_EQ(tracker.RegisterRetryAttempt(id, 2, 3, 1000), ReceiverDeliveryRetryRegisterStatus::kAttemptLimitReached);
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(id, 1), ReceiverDeliveryAckStatus::kConfirmed);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(PrivateDeliveryIdentity(2, 20), 4, 10), ReceiverDeliveryRegisterStatus::kRegistered);
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(2, 20, 4), ReceiverDeliveryAckStatus::kConfirmed);
        TINYIMX_EXPECT_EQ(tracker.GetWindowStats().attempt_sequences, static_cast<std::size_t>(1));
    });
    runner.Add("ReceiverDeliveryTracker.ConcurrentWindowAdmissionCannotExceedCapacity", []() {
        ReceiverDeliveryTracker::WindowOptions limits;
        limits.max_waiting_entries = 2;
        limits.max_waiting_per_recipient = 2;
        limits.max_waiting_bytes = 2;
        limits.max_bytes_per_recipient = 2;
        ReceiverDeliveryTracker tracker(10, limits);
        std::atomic<int> accepted{0}, blocked{0};
        std::vector<std::thread> threads;
        for (std::uint64_t m = 1; m <= 16; ++m) threads.emplace_back([&, m] {
            const auto status = tracker.RegisterAttempt(PrivateDeliveryIdentity(m, 10), static_cast<std::uint32_t>(m), 1);
            if (status == ReceiverDeliveryRegisterStatus::kRegistered) ++accepted;
            if (status == ReceiverDeliveryRegisterStatus::kWindowFull) ++blocked;
        });
        for (auto& thread : threads) thread.join();
        TINYIMX_EXPECT_EQ(accepted.load(), 2);
        TINYIMX_EXPECT_EQ(blocked.load(), 14);
        TINYIMX_EXPECT_EQ(tracker.GetWindowStats().waiting_entries, static_cast<std::size_t>(2));
        TINYIMX_EXPECT_EQ(tracker.GetWindowStats().waiting_bytes, static_cast<std::size_t>(2));
    });
    runner.Add("ReceiverDeliveryTracker.PrivateIndexEvictionAndGroupNamespace", []() {
        ReceiverDeliveryTracker tracker(1);
        const auto group = GroupDeliveryIdentity(4001, 77);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(group, 7), ReceiverDeliveryRegisterStatus::kRegistered);
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(group, 7), ReceiverDeliveryAckStatus::kConfirmed);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(4001, 88, 8), ReceiverDeliveryRegisterStatus::kRegistered);
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(4001, 88, 8), ReceiverDeliveryAckStatus::kConfirmed);
        ReceiverDeliverySnapshot snapshot;
        TINYIMX_EXPECT_TRUE(tracker.GetSnapshot(4001, &snapshot));
        TINYIMX_EXPECT_EQ(snapshot.receiver_user_id, static_cast<std::uint64_t>(88));
        TINYIMX_EXPECT_TRUE(!tracker.GetSnapshot(group, &snapshot));
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(4002, 99, 9), ReceiverDeliveryRegisterStatus::kRegistered);
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(4002, 99, 9), ReceiverDeliveryAckStatus::kConfirmed);
        TINYIMX_EXPECT_TRUE(!tracker.GetSnapshot(4001, &snapshot));
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(4001, 88, 8), ReceiverDeliveryAckStatus::kUnknownMessage);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(4001, 100, 10), ReceiverDeliveryRegisterStatus::kRegistered);
        TINYIMX_EXPECT_TRUE(tracker.GetSnapshot(4001, &snapshot));
        TINYIMX_EXPECT_EQ(snapshot.receiver_user_id, static_cast<std::uint64_t>(100));
    });
    runner.Add("ReceiverDeliveryTracker.GroupEvictionCannotErasePrivateIndex", []() {
        ReceiverDeliveryTracker tracker(1);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(4101, 88, 8), ReceiverDeliveryRegisterStatus::kRegistered);
        const auto group = GroupDeliveryIdentity(4101, 77);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(group, 9), ReceiverDeliveryRegisterStatus::kRegistered);
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(group, 9), ReceiverDeliveryAckStatus::kConfirmed);
        TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(GroupDeliveryIdentity(4102, 77), 10), ReceiverDeliveryRegisterStatus::kRegistered);
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(GroupDeliveryIdentity(4102, 77), 10), ReceiverDeliveryAckStatus::kConfirmed);
        ReceiverDeliverySnapshot snapshot;
        TINYIMX_EXPECT_TRUE(tracker.GetSnapshot(4101, &snapshot));
        TINYIMX_EXPECT_TRUE(!snapshot.confirmed);
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(4101, 89, 8), ReceiverDeliveryAckStatus::kReceiverMismatch);
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(4101, 88, 8), ReceiverDeliveryAckStatus::kConfirmed);
    });
    runner.Add("ReceiverDeliveryTracker.ConcurrentIndexHasOnePrivateReceiver", []() {
        ReceiverDeliveryTracker tracker;
        std::atomic<int> registered{0};
        std::atomic<int> mismatched{0};
        std::vector<std::thread> threads;
        for (std::uint64_t receiver = 1; receiver <= 8; ++receiver) {
            threads.emplace_back([&, receiver] {
                const auto status = tracker.RegisterAttempt(4201, receiver, static_cast<std::uint32_t>(receiver));
                if (status == ReceiverDeliveryRegisterStatus::kRegistered) ++registered;
                if (status == ReceiverDeliveryRegisterStatus::kReceiverMismatch) ++mismatched;
            });
        }
        for (auto& thread : threads) thread.join();
        TINYIMX_EXPECT_EQ(registered.load(), 1);
        TINYIMX_EXPECT_EQ(mismatched.load(), 7);
        TINYIMX_EXPECT_EQ(tracker.Size(), static_cast<std::size_t>(1));
        ReceiverDeliverySnapshot snapshot;
        TINYIMX_EXPECT_TRUE(tracker.GetSnapshot(4201, &snapshot));
        TINYIMX_EXPECT_EQ(tracker.Acknowledge(4201, snapshot.receiver_user_id, snapshot.current_delivery_seq),
                          ReceiverDeliveryAckStatus::kConfirmed);
    });
    runner.Add(
        "ReceiverDeliveryTracker."
        "RegistersFirstAttempt",
        []() {
            ReceiverDeliveryTracker tracker;


            TINYIMX_EXPECT_EQ(
                tracker.RegisterAttempt(
                    1001,
                    10002,
                    41
                ),
                ReceiverDeliveryRegisterStatus::
                    kRegistered
            );


            ReceiverDeliverySnapshot
                snapshot;


            TINYIMX_EXPECT_TRUE(
                tracker.GetSnapshot(
                    1001,
                    &snapshot
                )
            );


            TINYIMX_EXPECT_EQ(
                snapshot.message_id,
                static_cast<std::uint64_t>(
                    1001
                )
            );


            TINYIMX_EXPECT_EQ(
                snapshot.receiver_user_id,
                static_cast<std::uint64_t>(
                    10002
                )
            );


            TINYIMX_EXPECT_EQ(
                snapshot.current_delivery_seq,
                static_cast<std::uint32_t>(
                    41
                )
            );


            TINYIMX_EXPECT_EQ(
                snapshot.attempt_count,
                static_cast<std::uint32_t>(
                    1
                )
            );


            TINYIMX_EXPECT_TRUE(
                !snapshot.confirmed
            );
        }
    );


    runner.Add(
        "ReceiverDeliveryTracker."
        "RegistersRetryAttempt",
        []() {
            ReceiverDeliveryTracker tracker;


            TINYIMX_EXPECT_EQ(
                tracker.RegisterAttempt(
                    1002,
                    10002,
                    51
                ),
                ReceiverDeliveryRegisterStatus::
                    kRegistered
            );


            TINYIMX_EXPECT_EQ(
                tracker.RegisterAttempt(
                    1002,
                    10002,
                    52
                ),
                ReceiverDeliveryRegisterStatus::
                    kRetryRegistered
            );


            ReceiverDeliverySnapshot
                snapshot;


            TINYIMX_EXPECT_TRUE(
                tracker.GetSnapshot(
                    1002,
                    &snapshot
                )
            );


            TINYIMX_EXPECT_EQ(
                snapshot.current_delivery_seq,
                static_cast<std::uint32_t>(
                    52
                )
            );


            TINYIMX_EXPECT_EQ(
                snapshot.attempt_count,
                static_cast<std::uint32_t>(
                    2
                )
            );
        }
    );


    runner.Add(
        "ReceiverDeliveryTracker."
        "LateOldAttemptAckConfirms",
        []() {
            ReceiverDeliveryTracker tracker;


            TINYIMX_EXPECT_EQ(
                tracker.RegisterAttempt(
                    1003,
                    10002,
                    61
                ),
                ReceiverDeliveryRegisterStatus::
                    kRegistered
            );


            TINYIMX_EXPECT_EQ(
                tracker.RegisterAttempt(
                    1003,
                    10002,
                    62
                ),
                ReceiverDeliveryRegisterStatus::
                    kRetryRegistered
            );


            /*
             * 当前Attempt已经62，
             * 但旧的61迟到ACK仍然合法。
             */
            TINYIMX_EXPECT_EQ(
                tracker.Acknowledge(
                    1003,
                    10002,
                    61
                ),
                ReceiverDeliveryAckStatus::
                    kConfirmed
            );


            ReceiverDeliverySnapshot
                snapshot;


            TINYIMX_EXPECT_TRUE(
                tracker.GetSnapshot(
                    1003,
                    &snapshot
                )
            );


            TINYIMX_EXPECT_TRUE(
                snapshot.confirmed
            );
        }
    );


    runner.Add(
        "ReceiverDeliveryTracker."
        "DuplicateAckIsIdempotent",
        []() {
            ReceiverDeliveryTracker tracker;


            TINYIMX_EXPECT_EQ(
                tracker.RegisterAttempt(
                    1004,
                    10002,
                    71
                ),
                ReceiverDeliveryRegisterStatus::
                    kRegistered
            );


            TINYIMX_EXPECT_EQ(
                tracker.Acknowledge(
                    1004,
                    10002,
                    71
                ),
                ReceiverDeliveryAckStatus::
                    kConfirmed
            );


            TINYIMX_EXPECT_EQ(
                tracker.Acknowledge(
                    1004,
                    10002,
                    71
                ),
                ReceiverDeliveryAckStatus::
                    kDuplicate
            );
        }
    );


    runner.Add(
        "ReceiverDeliveryTracker."
        "RejectsWrongReceiverAndUnknownAttempt",
        []() {
            ReceiverDeliveryTracker tracker;


            TINYIMX_EXPECT_EQ(
                tracker.RegisterAttempt(
                    1005,
                    10002,
                    81
                ),
                ReceiverDeliveryRegisterStatus::
                    kRegistered
            );


            TINYIMX_EXPECT_EQ(
                tracker.Acknowledge(
                    1005,
                    10003,
                    81
                ),
                ReceiverDeliveryAckStatus::
                    kReceiverMismatch
            );


            TINYIMX_EXPECT_EQ(
                tracker.Acknowledge(
                    1005,
                    10002,
                    999
                ),
                ReceiverDeliveryAckStatus::
                    kUnknownAttempt
            );
        }
    );


    runner.Add(
        "ReceiverDeliveryTracker."
        "RejectsReceiverIdentityConflict",
        []() {
            ReceiverDeliveryTracker tracker;


            TINYIMX_EXPECT_EQ(
                tracker.RegisterAttempt(
                    1006,
                    10002,
                    91
                ),
                ReceiverDeliveryRegisterStatus::
                    kRegistered
            );


            TINYIMX_EXPECT_EQ(
                tracker.RegisterAttempt(
                    1006,
                    10003,
                    92
                ),
                ReceiverDeliveryRegisterStatus::
                    kReceiverMismatch
            );


            TINYIMX_EXPECT_EQ(
                tracker.RegisterRetryAttempt(
                    1006,
                    10003,
                    91,
                    93,
                    3
                ),
                ReceiverDeliveryRetryRegisterStatus::
                    kReceiverMismatch
            );
        }
    );


    runner.Add(
        "ReceiverDeliveryTracker."
        "BoundsConfirmedCache",
        []() {
            ReceiverDeliveryTracker
                tracker(2);


            for (
                std::uint64_t message_id :
                {
                    2001ULL,
                    2002ULL,
                    2003ULL
                }
            ) {
                const std::uint32_t seq =
                    static_cast<std::uint32_t>(
                        message_id
                    );


                TINYIMX_EXPECT_EQ(
                    tracker.RegisterAttempt(
                        message_id,
                        10002,
                        seq
                    ),
                    ReceiverDeliveryRegisterStatus::
                        kRegistered
                );


                TINYIMX_EXPECT_EQ(
                    tracker.Acknowledge(
                        message_id,
                        10002,
                        seq
                    ),
                    ReceiverDeliveryAckStatus::
                        kConfirmed
                );
            }


            TINYIMX_EXPECT_EQ(
                tracker.ConfirmedCount(),
                static_cast<std::size_t>(
                    2
                )
            );


            ReceiverDeliverySnapshot
                snapshot;


            /*
             * 最老的2001已被热缓存淘汰。
             */
            TINYIMX_EXPECT_TRUE(
                !tracker.GetSnapshot(
                    2001,
                    &snapshot
                )
            );


            TINYIMX_EXPECT_TRUE(
                tracker.GetSnapshot(
                    2002,
                    &snapshot
                )
            );


            TINYIMX_EXPECT_TRUE(
                tracker.GetSnapshot(
                    2003,
                    &snapshot
                )
            );
        }
    );

    runner.Add(
        "ReceiverDeliveryTracker."
        "RegistersRetryAfterCurrentTimeout",
        []() {
            ReceiverDeliveryTracker tracker;


            TINYIMX_EXPECT_EQ(
                tracker.RegisterAttempt(
                    3001,
                    10002,
                    101
                ),
                ReceiverDeliveryRegisterStatus::
                    kRegistered
            );


            TINYIMX_EXPECT_EQ(
                tracker.RegisterRetryAttempt(
                    3001,
                    10002,
                    101,
                    102,
                    3
                ),
                ReceiverDeliveryRetryRegisterStatus::
                    kRetryRegistered
            );


            ReceiverDeliverySnapshot snapshot;


            TINYIMX_EXPECT_TRUE(
                tracker.GetSnapshot(
                    3001,
                    &snapshot
                )
            );


            TINYIMX_EXPECT_EQ(
                snapshot.current_delivery_seq,
                static_cast<std::uint32_t>(
                    102
                )
            );


            TINYIMX_EXPECT_EQ(
                snapshot.attempt_count,
                static_cast<std::uint32_t>(
                    2
                )
            );
        }
    );


    runner.Add(
        "ReceiverDeliveryTracker."
        "RejectsStaleTimeoutRetry",
        []() {
            ReceiverDeliveryTracker tracker;


            TINYIMX_EXPECT_EQ(
                tracker.RegisterAttempt(
                    3002,
                    10002,
                    201
                ),
                ReceiverDeliveryRegisterStatus::
                    kRegistered
            );


            TINYIMX_EXPECT_EQ(
                tracker.RegisterRetryAttempt(
                    3002,
                    10002,
                    201,
                    202,
                    3
                ),
                ReceiverDeliveryRetryRegisterStatus::
                    kRetryRegistered
            );


            /*
            * D201对应的旧timeout又迟到。
            *
            * current已经是202。
            */
            TINYIMX_EXPECT_EQ(
                tracker.RegisterRetryAttempt(
                    3002,
                    10002,
                    201,
                    203,
                    3
                ),
                ReceiverDeliveryRetryRegisterStatus::
                    kStaleAttempt
            );


            ReceiverDeliverySnapshot snapshot;


            TINYIMX_EXPECT_TRUE(
                tracker.GetSnapshot(
                    3002,
                    &snapshot
                )
            );


            TINYIMX_EXPECT_EQ(
                snapshot.current_delivery_seq,
                static_cast<std::uint32_t>(
                    202
                )
            );


            TINYIMX_EXPECT_EQ(
                snapshot.attempt_count,
                static_cast<std::uint32_t>(
                    2
                )
            );
        }
    );

    runner.Add(
        "ReceiverDeliveryTracker."
        "EnforcesRetryAttemptLimit",
        []() {
            ReceiverDeliveryTracker tracker;


            TINYIMX_EXPECT_EQ(
                tracker.RegisterAttempt(
                    3003,
                    10002,
                    301
                ),
                ReceiverDeliveryRegisterStatus::
                    kRegistered
            );


            /*
            * max_attempts=2
            *
            * D301是第一次，
            * D302是最后一次允许Retry。
            */
            TINYIMX_EXPECT_EQ(
                tracker.RegisterRetryAttempt(
                    3003,
                    10002,
                    301,
                    302,
                    2
                ),
                ReceiverDeliveryRetryRegisterStatus::
                    kRetryRegistered
            );


            TINYIMX_EXPECT_EQ(
                tracker.RegisterRetryAttempt(
                    3003,
                    10002,
                    302,
                    303,
                    2
                ),
                ReceiverDeliveryRetryRegisterStatus::
                    kAttemptLimitReached
            );


            ReceiverDeliverySnapshot snapshot;


            TINYIMX_EXPECT_TRUE(
                tracker.GetSnapshot(
                    3003,
                    &snapshot
                )
            );


            TINYIMX_EXPECT_EQ(
                snapshot.current_delivery_seq,
                static_cast<std::uint32_t>(
                    302
                )
            );


            TINYIMX_EXPECT_EQ(
                snapshot.attempt_count,
                static_cast<std::uint32_t>(
                    2
                )
            );
        }
    );


    runner.Add(
        "ReceiverDeliveryTracker."
        "LateAckAfterRetryStillConfirms",
        []() {
            ReceiverDeliveryTracker tracker;


            TINYIMX_EXPECT_EQ(
                tracker.RegisterAttempt(
                    3004,
                    10002,
                    401
                ),
                ReceiverDeliveryRegisterStatus::
                    kRegistered
            );


            TINYIMX_EXPECT_EQ(
                tracker.RegisterRetryAttempt(
                    3004,
                    10002,
                    401,
                    402,
                    3
                ),
                ReceiverDeliveryRetryRegisterStatus::
                    kRetryRegistered
            );


            /*
            * D402已经发出，
            * 但迟到的D401 ACK仍然合法。
            */
            TINYIMX_EXPECT_EQ(
                tracker.Acknowledge(
                    3004,
                    10002,
                    401
                ),
                ReceiverDeliveryAckStatus::
                    kConfirmed
            );


            ReceiverDeliverySnapshot snapshot;


            TINYIMX_EXPECT_TRUE(
                tracker.GetSnapshot(
                    3004,
                    &snapshot
                )
            );


            TINYIMX_EXPECT_TRUE(
                snapshot.confirmed
            );
        }
    );


    runner.Add(
        "ReceiverDeliveryTracker."
        "ConfirmedMessageCannotRetry",
        []() {
            ReceiverDeliveryTracker tracker;


            TINYIMX_EXPECT_EQ(
                tracker.RegisterAttempt(
                    3005,
                    10002,
                    501
                ),
                ReceiverDeliveryRegisterStatus::
                    kRegistered
            );


            TINYIMX_EXPECT_EQ(
                tracker.Acknowledge(
                    3005,
                    10002,
                    501
                ),
                ReceiverDeliveryAckStatus::
                    kConfirmed
            );


            TINYIMX_EXPECT_EQ(
                tracker.RegisterRetryAttempt(
                    3005,
                    10002,
                    501,
                    502,
                    3
                ),
                ReceiverDeliveryRetryRegisterStatus::
                    kAlreadyConfirmed
            );


            ReceiverDeliverySnapshot snapshot;


            TINYIMX_EXPECT_TRUE(
                tracker.GetSnapshot(
                    3005,
                    &snapshot
                )
            );


            TINYIMX_EXPECT_TRUE(
                snapshot.confirmed
            );


            TINYIMX_EXPECT_EQ(
                snapshot.attempt_count,
                static_cast<std::uint32_t>(
                    1
                )
            );
        }
    );


    runner.Add(
        "ReceiverDeliveryTracker.M17B2GroupRecipientsAreIndependent",
        []() {
            ReceiverDeliveryTracker tracker;
            const auto u2 = GroupDeliveryIdentity(77, 10002);
            const auto u3 = GroupDeliveryIdentity(77, 10003);
            TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(u2, 11), ReceiverDeliveryRegisterStatus::kRegistered);
            TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(u3, 12), ReceiverDeliveryRegisterStatus::kRegistered);
            TINYIMX_EXPECT_EQ(tracker.Acknowledge(u2, 11), ReceiverDeliveryAckStatus::kConfirmed);
            ReceiverDeliverySnapshot u2_snapshot;
            ReceiverDeliverySnapshot u3_snapshot;
            TINYIMX_EXPECT_TRUE(tracker.GetSnapshot(u2, &u2_snapshot));
            TINYIMX_EXPECT_TRUE(tracker.GetSnapshot(u3, &u3_snapshot));
            TINYIMX_EXPECT_TRUE(u2_snapshot.confirmed);
            TINYIMX_EXPECT_TRUE(!u3_snapshot.confirmed);
        });

    runner.Add(
        "ReceiverDeliveryTracker.M17B2PrivateAndGroupNamespacesDoNotCollide",
        []() {
            ReceiverDeliveryTracker tracker;
            const auto private_id = PrivateDeliveryIdentity(88, 10002);
            const auto group_id = GroupDeliveryIdentity(88, 10002);
            TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(private_id, 21), ReceiverDeliveryRegisterStatus::kRegistered);
            TINYIMX_EXPECT_EQ(tracker.RegisterAttempt(group_id, 22), ReceiverDeliveryRegisterStatus::kRegistered);
            TINYIMX_EXPECT_EQ(tracker.Size(), static_cast<std::size_t>(2));
        });


}


}  // namespace tinyimx::test
