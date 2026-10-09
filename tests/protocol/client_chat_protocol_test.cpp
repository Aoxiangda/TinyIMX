#include "common/protocol/ClientChatProtocol.h"
#include "common/protocol/Packet.h"
#include "tests/concurrency/TestFramework.h"

#include <string>

namespace tinyimx::test{
    void RegisterClientChatProtocolTests(
    TestRunner& runner
) {
    runner.Add(
        "ClientChatProtocol.RequestRoundTrip",
        []() {
            tinyimx::ClientChatRequest request;

            request.client_message_id =
                "client-msg-001";

            request.to_user_id =
                10002;

            request.text =
                "hello tinyimx";


            std::string body;
            std::string error;


            TINYIMX_EXPECT_TRUE(
                tinyimx::
                    SerializeClientChatRequest(
                        request,
                        &body,
                        &error
                    )
            );


            tinyimx::ClientChatRequest parsed;


            TINYIMX_EXPECT_TRUE(
                tinyimx::
                    DeserializeClientChatRequest(
                        body,
                        &parsed,
                        &error
                    )
            );


            TINYIMX_EXPECT_EQ(
                parsed.client_message_id,
                request.client_message_id
            );

            TINYIMX_EXPECT_EQ(
                parsed.to_user_id,
                request.to_user_id
            );

            TINYIMX_EXPECT_EQ(
                parsed.text,
                request.text
            );

            TINYIMX_EXPECT_TRUE(
                !parsed.
                    claimed_from_user_id.
                    has_value()
            );
        }
    );


    runner.Add(
        "ClientChatProtocol.OptionalClaimedFromRoundTrip",
        []() {
            tinyimx::ClientChatRequest request;

            request.client_message_id =
                "client-msg-002";

            request.to_user_id =
                10002;

            request.text =
                "compatibility";

            request.claimed_from_user_id =
                10001;


            std::string body;
            std::string error;


            TINYIMX_EXPECT_TRUE(
                tinyimx::
                    SerializeClientChatRequest(
                        request,
                        &body,
                        &error
                    )
            );


            tinyimx::ClientChatRequest parsed;


            TINYIMX_EXPECT_TRUE(
                tinyimx::
                    DeserializeClientChatRequest(
                        body,
                        &parsed,
                        &error
                    )
            );


            TINYIMX_EXPECT_TRUE(
                parsed.
                    claimed_from_user_id.
                    has_value()
            );

            TINYIMX_EXPECT_EQ(
                parsed.
                    claimed_from_user_id.
                    value(),
                std::uint64_t{10001}
            );
        }
    );


    runner.Add(
        "ClientChatProtocol.RejectsMissingClientMessageId",
        []() {
            tinyimx::ClientChatRequest parsed;

            std::string error;


            TINYIMX_EXPECT_TRUE(
                !tinyimx::
                    DeserializeClientChatRequest(
                        R"({"to":10002,"text":"hello"})",
                        &parsed,
                        &error
                    )
            );


            TINYIMX_EXPECT_TRUE(
                !error.empty()
            );
        }
    );


    runner.Add(
        "ClientChatProtocol.RejectsTooLongClientMessageId",
        []() {
            tinyimx::ClientChatRequest request;

            request.client_message_id =
                std::string(
                    tinyimx::
                        kMaxClientMessageIdSize + 1,
                    'x'
                );

            request.to_user_id =
                10002;

            request.text =
                "hello";


            std::string body;
            std::string error;


            TINYIMX_EXPECT_TRUE(
                !tinyimx::
                    SerializeClientChatRequest(
                        request,
                        &body,
                        &error
                    )
            );


            TINYIMX_EXPECT_TRUE(
                !error.empty()
            );
        }
    );


    runner.Add(
        "ClientChatProtocol.AckRoundTrip",
        []() {
            tinyimx::ClientChatAck ack;

            ack.success =
                true;

            ack.delivered =
                true;

            ack.stored_offline =
                false;

            ack.stored_persistent =
                true;

            ack.reused =
                true;

            ack.client_message_id =
                "client-msg-003";

            ack.message_id =
                77;

            ack.from_user_id =
                10001;

            ack.to_user_id =
                10002;

            ack.receiver_private_unread =
                8;

            ack.receiver_total_unread =
                11;

            ack.reason =
                "remote_delivered";

            ack.remote_gateway_id =
                "gateway-b";

            ack.remote_host =
                "127.0.0.1";

            ack.remote_port =
                9002;


            std::string body;
            std::string error;


            TINYIMX_EXPECT_TRUE(
                tinyimx::
                    SerializeClientChatAck(
                        ack,
                        &body,
                        &error
                    )
            );


            tinyimx::ClientChatAck parsed;


            TINYIMX_EXPECT_TRUE(
                tinyimx::
                    DeserializeClientChatAck(
                        body,
                        &parsed,
                        &error
                    )
            );


            TINYIMX_EXPECT_TRUE(
                parsed.success
            );

            TINYIMX_EXPECT_TRUE(
                parsed.delivered
            );

            TINYIMX_EXPECT_TRUE(
                parsed.reused
            );

            TINYIMX_EXPECT_EQ(
                parsed.client_message_id,
                ack.client_message_id
            );

            TINYIMX_EXPECT_EQ(
                parsed.message_id,
                std::uint64_t{77}
            );

            TINYIMX_EXPECT_EQ(
                parsed.from_user_id,
                std::uint64_t{10001}
            );

            TINYIMX_EXPECT_EQ(
                parsed.to_user_id,
                std::uint64_t{10002}
            );

            TINYIMX_EXPECT_EQ(
                parsed.remote_gateway_id,
                std::string("gateway-b")
            );
        }
    );


    runner.Add(
        "ClientChatProtocol.RejectsSuccessfulAckWithoutMessageId",
        []() {
            tinyimx::ClientChatAck ack;

            ack.success =
                true;

            ack.client_message_id =
                "client-msg-004";

            ack.message_id =
                0;

            ack.from_user_id =
                10001;

            ack.to_user_id =
                10002;


            std::string body;
            std::string error;


            TINYIMX_EXPECT_TRUE(
                !tinyimx::
                    SerializeClientChatAck(
                        ack,
                        &body,
                        &error
                    )
            );


            TINYIMX_EXPECT_TRUE(
                !error.empty()
            );
        }
    );

    runner.Add(
    "ClientChatProtocol.DeliveryMessageTypes",
    []() {
        TINYIMX_EXPECT_TRUE(
            tinyimx::IsKnownMessageType(
                tinyimx::MessageType::
                    kChatDelivery
            )
        );

        TINYIMX_EXPECT_TRUE(
            tinyimx::IsKnownMessageType(
                tinyimx::MessageType::
                    kChatDeliveryAck
            )
        );

        TINYIMX_EXPECT_TRUE(
            !tinyimx::
                IsGatewayInternalMessageType(
                    tinyimx::MessageType::
                        kChatDelivery
                )
        );

        TINYIMX_EXPECT_TRUE(
            !tinyimx::
                IsGatewayInternalMessageType(
                    tinyimx::MessageType::
                        kChatDeliveryAck
                )
        );

        TINYIMX_EXPECT_EQ(
            tinyimx::MessageTypeToString(
                tinyimx::MessageType::
                    kChatDelivery
            ),
            std::string("chat_delivery")
        );

        TINYIMX_EXPECT_EQ(
            tinyimx::MessageTypeToString(
                tinyimx::MessageType::
                    kChatDeliveryAck
            ),
            std::string(
                "chat_delivery_ack"
            )
        );
    }
);


runner.Add(
    "ClientChatProtocol.DeliveryRoundTrip",
    []() {
        tinyimx::ServerChatDelivery
            delivery;

        delivery.message_id =
            105;

        delivery.from_user_id =
            10001;

        delivery.to_user_id =
            10002;

        delivery.text =
            "receiver delivery";


        std::string body;
        std::string error;


        TINYIMX_EXPECT_TRUE(
            tinyimx::
                SerializeServerChatDelivery(
                    delivery,
                    &body,
                    &error
                )
        );


        /*
         * seq不属于body；
         * client_message_id也不属于
         * Receiver Delivery Domain。
         */
        TINYIMX_EXPECT_TRUE(
            body.find("\"seq\"") ==
                std::string::npos
        );

        TINYIMX_EXPECT_TRUE(
            body.find(
                "client_message_id"
            ) ==
                std::string::npos
        );


        tinyimx::ServerChatDelivery
            parsed;


        TINYIMX_EXPECT_TRUE(
            tinyimx::
                DeserializeServerChatDelivery(
                    body,
                    &parsed,
                    &error
                )
        );


        TINYIMX_EXPECT_EQ(
            parsed.message_id,
            std::uint64_t{105}
        );

        TINYIMX_EXPECT_EQ(
            parsed.from_user_id,
            std::uint64_t{10001}
        );

        TINYIMX_EXPECT_EQ(
            parsed.to_user_id,
            std::uint64_t{10002}
        );

        TINYIMX_EXPECT_EQ(
            parsed.text,
            std::string(
                "receiver delivery"
            )
        );
    }
);


runner.Add(
    "ClientChatProtocol.DeliveryRejectsZeroMessageId",
    []() {
        tinyimx::ServerChatDelivery
            delivery;

        delivery.message_id =
            0;

        delivery.from_user_id =
            10001;

        delivery.to_user_id =
            10002;

        delivery.text =
            "invalid";


        std::string body;
        std::string error;


        TINYIMX_EXPECT_TRUE(
            !tinyimx::
                SerializeServerChatDelivery(
                    delivery,
                    &body,
                    &error
                )
        );

        TINYIMX_EXPECT_TRUE(
            !error.empty()
        );
    }
);


runner.Add(
    "ClientChatProtocol.DeliveryRejectsEmptyText",
    []() {
        tinyimx::ServerChatDelivery
            delivery;

        delivery.message_id =
            105;

        delivery.from_user_id =
            10001;

        delivery.to_user_id =
            10002;


        std::string body;
        std::string error;


        TINYIMX_EXPECT_TRUE(
            !tinyimx::
                SerializeServerChatDelivery(
                    delivery,
                    &body,
                    &error
                )
        );

        TINYIMX_EXPECT_TRUE(
            !error.empty()
        );
    }
);


runner.Add(
    "ClientChatProtocol.DeliveryRejectsMissingMessageId",
    []() {
        tinyimx::ServerChatDelivery
            parsed;

        std::string error;


        TINYIMX_EXPECT_TRUE(
            !tinyimx::
                DeserializeServerChatDelivery(
                    R"({"from":10001,"to":10002,"text":"hello"})",
                    &parsed,
                    &error
                )
        );

        TINYIMX_EXPECT_TRUE(
            !error.empty()
        );
    }
);


runner.Add(
    "ClientChatProtocol.DeliveryRejectsStringMessageId",
    []() {
        tinyimx::ServerChatDelivery
            parsed;

        std::string error;


        TINYIMX_EXPECT_TRUE(
            !tinyimx::
                DeserializeServerChatDelivery(
                    R"({"message_id":"105","from":10001,"to":10002,"text":"hello"})",
                    &parsed,
                    &error
                )
        );

        TINYIMX_EXPECT_TRUE(
            !error.empty()
        );
    }
);


runner.Add(
    "ClientChatProtocol.DeliveryRejectsMalformedJson",
    []() {
        tinyimx::ServerChatDelivery
            parsed;

        std::string error;


        TINYIMX_EXPECT_TRUE(
            !tinyimx::
                DeserializeServerChatDelivery(
                    R"({"message_id":105)",
                    &parsed,
                    &error
                )
        );

        TINYIMX_EXPECT_TRUE(
            !error.empty()
        );
    }
);


runner.Add(
    "ClientChatProtocol.DeliveryAckRoundTrip",
    []() {
        tinyimx::ReceiverChatDeliveryAck
            ack;

        ack.message_id =
            105;


        std::string body;
        std::string error;


        TINYIMX_EXPECT_TRUE(
            tinyimx::
                SerializeReceiverChatDeliveryAck(
                    ack,
                    &body,
                    &error
                )
        );


        TINYIMX_EXPECT_TRUE(
            body.find(
                "receiver_user_id"
            ) ==
                std::string::npos
        );

        TINYIMX_EXPECT_TRUE(
            body.find(
                "client_message_id"
            ) ==
                std::string::npos
        );


        tinyimx::ReceiverChatDeliveryAck
            parsed;


        TINYIMX_EXPECT_TRUE(
            tinyimx::
                DeserializeReceiverChatDeliveryAck(
                    body,
                    &parsed,
                    &error
                )
        );


        TINYIMX_EXPECT_EQ(
            parsed.message_id,
            std::uint64_t{105}
        );
    }
);


runner.Add(
    "ClientChatProtocol.DeliveryAckRejectsZeroMessageId",
    []() {
        tinyimx::ReceiverChatDeliveryAck
            ack;

        ack.message_id =
            0;


        std::string body;
        std::string error;


        TINYIMX_EXPECT_TRUE(
            !tinyimx::
                SerializeReceiverChatDeliveryAck(
                    ack,
                    &body,
                    &error
                )
        );

        TINYIMX_EXPECT_TRUE(
            !error.empty()
        );
    }
);


runner.Add(
    "ClientChatProtocol.DeliveryAckRejectsMissingMessageId",
    []() {
        tinyimx::ReceiverChatDeliveryAck
            parsed;

        std::string error;


        TINYIMX_EXPECT_TRUE(
            !tinyimx::
                DeserializeReceiverChatDeliveryAck(
                    R"({})",
                    &parsed,
                    &error
                )
        );

        TINYIMX_EXPECT_TRUE(
            !error.empty()
        );
    }
);


}
}
