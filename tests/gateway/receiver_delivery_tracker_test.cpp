#include "tests/concurrency/TestFramework.h"

#include "gateway/ReceiverDeliveryTracker.h"


namespace tinyimx::test {


void
RegisterReceiverDeliveryTrackerTests(
    TestRunner& runner
) {
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


}


}  // namespace tinyimx::test