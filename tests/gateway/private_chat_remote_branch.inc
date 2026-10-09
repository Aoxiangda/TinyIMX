            // R2A: acknowledge verified durable acceptance before initiating
            // remote delivery. Keep the unread projection snapshot contract.
            // Peer outcome cannot revoke acceptance or emit a second Chat ACK.
            rpc::PersistPrivateMessageRpcRequest expected_persist;
            expected_persist.from_user_id = from_user_id;
            expected_persist.to_user_id = to_user_id;
            expected_persist.client_message_id = request.client_message_id;
            expected_persist.message_type = static_cast<std::uint32_t>(
                rpc::PrivateMessageContentType::kText);
            expected_persist.content = server_body_text;
            const auto durable_ack = BuildRemoteDurableAcceptance(
                persist_call, expected_persist,
                receiver_private_unread, receiver_total_unread,
                remote_gateway.gateway_id, remote_gateway.listen_host,
                remote_gateway.listen_port);

            if (!durable_ack) {
                // Adapter accepted an inconsistent record: do not invent a
                // successful ACK or delivery. The attempted write is uncertain.
                phase_trace.Fail();
                ClientChatAck invalid_ack;
                invalid_ack.client_message_id = request.client_message_id;
                invalid_ack.from_user_id = from_user_id;
                invalid_ack.to_user_id = to_user_id;
                invalid_ack.reason = "message_persistence_uncertain";
                send_client_chat_ack(invalid_ack);
                LOG_ERROR("gateway remote durable acceptance identity invalid"
                    << ", message_id=" << server_message_id);
                return;
            }

            const auto acceptance_dispatch = DispatchRemoteDurableAcceptance(
                durable_ack,
                [&](const ClientChatAck& accepted_ack) {
                    return send_client_chat_ack(accepted_ack);
                },
                [&] {
                    return gateway_peer_transport_manager_->ForwardChat(
                        remote_gateway, server_message_id, from_user_id,
                        to_user_id, server_body_text,
                        // Delivery observer holds no GatewayServer, Session or
                        // sender connection. It never calls MessageService and
                        // never queues gateway.chat.remote_complete.
                        [from_user_id, to_user_id, server_message_id,
                         remote_gateway_id = remote_gateway.gateway_id](
                            GatewayPeerTransportResult result) {
                            const bool peer_delivery_submitted =
                                result.Succeeded() && result.response.Delivered();
                            LOG_INFO("gateway remote delivery attempt completed"
                                << ", from=" << from_user_id
                                << ", to=" << to_user_id
                                << ", message_id=" << server_message_id
                                << ", remote_gateway=" << remote_gateway_id
                                << ", peer_delivery_submitted=" << peer_delivery_submitted
                                << ", sender_ack_boundary=durable_acceptance"
                                << ", durable_convergence_not_proven=1");
                            if (!peer_delivery_submitted) {
                                LOG_WARN("gateway durable delivery still needs recovery"
                                    << ", message_id=" << server_message_id
                                    << ", to=" << to_user_id);
                            }
                        });
                });
            LOG_INFO("gateway remote durable acceptance replied"
                << ", client_message_id=" << request.client_message_id
                << ", message_id=" << server_message_id
                << ", from=" << from_user_id
                << ", to=" << to_user_id
                << ", reused=" << client_request_reused
                << ", ack_handoff_ok=" << acceptance_dispatch.ack_handoff_ok
                << ", forward_submitted=" << acceptance_dispatch.forward_submitted);
            if (!acceptance_dispatch.forward_submitted) {
                // The one Sender response has already been attempted. Keep the
                // durable Pending state; do not fabricate ReceiverConfirmed.
                LOG_WARN("gateway remote submission failed after durable acceptance"
                    << ", message_id=" << server_message_id
                    << ", to=" << to_user_id);
            }

