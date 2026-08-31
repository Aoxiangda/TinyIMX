cmake_minimum_required(VERSION 3.16)

project(TinyIMX LANGUAGES CXX)

include (CTest)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(nlohmann_json CONFIG REQUIRED)

# M1: Config 模块
add_library(tinyimx_config
    common/config/Config.cpp
)

target_include_directories(tinyimx_config PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_config PUBLIC
    cxx_std_20
)

target_link_libraries(tinyimx_config PUBLIC
    nlohmann_json::nlohmann_json
)

# M2: Logger 模块
add_library(tinyimx_logging
    common/logging/LogLevel.cpp
    common/logging/LogFormatter.cpp
    common/logging/ConsoleSink.cpp
    common/logging/FileSink.cpp
    common/logging/Logger.cpp
)

target_include_directories(tinyimx_logging PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_logging PUBLIC
    cxx_std_20
)

target_link_libraries(tinyimx_logging PUBLIC
    tinyimx_config
)


add_executable(config_demo
    examples/config_demo.cpp
)

target_link_libraries(config_demo PRIVATE
    tinyimx_config
)


# M13-C1: Business Runtime configuration compatibility/validation tests.
add_executable(config_business_runtime_tests
    tests/config/config_business_runtime_test.cpp
)

target_link_libraries(config_business_runtime_tests PRIVATE
    tinyimx_config
)

target_include_directories(config_business_runtime_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(run_config_business_runtime_tests
    COMMAND $<TARGET_FILE:config_business_runtime_tests>
    DEPENDS config_business_runtime_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)


add_executable(logger_demo
    examples/logger_demo.cpp
)

target_link_libraries(logger_demo PRIVATE
    tinyimx_config
    tinyimx_logging
)

# ============================================================
# M3: ThreadPool / Concurrency 模块
# ============================================================
add_library(tinyimx_concurrency
    common/concurrency/ThreadPoolTypes.cpp
    common/concurrency/ThreadPool.cpp
)

# 先给库设置属性（include、compile、link）
target_include_directories(tinyimx_concurrency PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_concurrency PUBLIC
    cxx_std_20
)

# 库只需要链接它依赖的其他模块，不能链接自己
target_link_libraries(tinyimx_concurrency PUBLIC
    tinyimx_config
    tinyimx_logging
)

# 再定义demo程序
add_executable(concurrency_demo
    examples/thread_pool_demo.cpp
)

# demo程序链接concurrency库，自动继承它的include路径和依赖
target_link_libraries(concurrency_demo PRIVATE
    tinyimx_concurrency
)

add_executable(thread_pool_benchmark
    benchmark/thread_pool_benchmark.cpp
    benchmark/thread_pool_benchmark_main.cpp
)

target_link_libraries(thread_pool_benchmark PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_concurrency
)
# ============================================================
# M3: ThreadPool / Concurrency 单元测试
# ============================================================

add_executable(concurrency_tests
    tests/concurrency/concurrency_test_main.cpp
    tests/concurrency/circular_queue_test.cpp
    tests/concurrency/mpmc_blocking_queue_test.cpp
    tests/concurrency/thread_pool_test.cpp
)

target_link_libraries(concurrency_tests PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_concurrency
)

target_include_directories(concurrency_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(run_concurrency_tests
    COMMAND $<TARGET_FILE:concurrency_tests>
    DEPENDS concurrency_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)


# ============================================================
# M4: Network 模块
# ============================================================
add_executable(buffer_demo
    examples/buffer_demo.cpp
)

target_link_libraries(buffer_demo PRIVATE
    tinyimx_net
)

add_library(
    tinyimx_net
    STATIC
        common/net/InetAddress.cpp
        common/net/Socket.cpp
        common/net/Buffer.cpp
        common/net/Channel.cpp
        common/net/EventLoop.cpp
        common/net/Timer.cpp
        common/net/TimerQueue.cpp
        common/net/EventLoopThread.cpp
        common/net/EventLoopThreadPool.cpp
        common/net/TcpConnection.cpp
        common/net/TcpServer.cpp
        common/net/Connector.cpp
        common/net/TcpClient.cpp
)

target_include_directories(tinyimx_net PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_net PUBLIC
    cxx_std_20
)

target_link_libraries(tinyimx_net PUBLIC
    tinyimx_logging
)

add_executable(socket_basic_demo
    examples/socket_basic_demo.cpp
)

target_link_libraries(socket_basic_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_net
)

# ============================================================
# M4: Network 模块单元测试
# ============================================================


add_executable(event_loop_demo
    examples/event_loop_demo.cpp
)

target_link_libraries(event_loop_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_net
)




add_executable(tcp_connection_demo
    examples/tcp_connection_demo.cpp
)

target_link_libraries(tcp_connection_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_net
)

add_executable(echo_server_demo
    examples/echo_server_demo.cpp
)

target_link_libraries(echo_server_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_net
)

# =============================================
# Net_Tests
# =============================================

add_executable(net_tests
    tests/net/net_test_main.cpp
    tests/net/inet_address_test.cpp
    tests/net/buffer_test.cpp
    tests/net/event_loop_test.cpp
    tests/net/tcp_server_test.cpp
    tests/net/tcp_client_test.cpp
)

target_link_libraries(net_tests PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_net
)

target_include_directories(net_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(run_net_tests
    COMMAND $<TARGET_FILE:net_tests>
    DEPENDS net_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(tcp_echo_stress_client
    benchmark/tcp_echo_stress_client.cpp
)

target_compile_features(tcp_echo_stress_client PRIVATE
    cxx_std_20
)


# ==========================================
# Protocol
# ==========================================
add_library(tinyimx_protocol
    common/protocol/Packet.cpp
    common/protocol/ProtocolCodec.cpp
    common/protocol/GatewayPeerProtocol.cpp
    common/protocol/ClientChatProtocol.cpp
)

target_include_directories(tinyimx_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_protocol PUBLIC
    cxx_std_20
)

target_link_libraries(tinyimx_protocol PUBLIC
    tinyimx_net
    nlohmann_json::nlohmann_json
)

add_executable(protocol_codec_demo
    examples/protocol_codec_demo.cpp
)

target_link_libraries(protocol_codec_demo PRIVATE
    tinyimx_protocol
)


add_executable(protocol_tests
    tests/protocol/protocol_test_main.cpp
    tests/protocol/protocol_codec_test.cpp
    tests/protocol/gateway_peer_protocol_test.cpp
    tests/protocol/client_chat_protocol_test.cpp
)

target_link_libraries(protocol_tests PRIVATE
    tinyimx_protocol
)

target_include_directories(protocol_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(run_protocol_tests
    COMMAND $<TARGET_FILE:protocol_tests>
    DEPENDS protocol_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)



add_executable(protocol_echo_server_demo
    examples/protocol_echo_server_demo.cpp
)

target_link_libraries(protocol_echo_server_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_net
    tinyimx_protocol
)

add_executable(protocol_echo_client_demo
    examples/protocol_echo_client_demo.cpp
)

target_link_libraries(protocol_echo_client_demo PRIVATE
    tinyimx_protocol
)

# ===========================================
# Gateway
# ===========================================
add_library(tinyimx_gateway
    gateway/GatewayServer.cpp
    gateway/business/BusinessExecutor.cpp
    gateway/SessionManager.cpp
    gateway/OfflineMessageStore.cpp
    gateway/GatewayRouteResolver.cpp
    gateway/GatewayPeerTransport.cpp
    gateway/GatewayPeerTransportManager.cpp
    gateway/MessageDeliveryDeduplicator.cpp
    gateway/ReceiverDeliveryTracker.cpp
)

target_include_directories(tinyimx_gateway PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_gateway PUBLIC
    cxx_std_20
)

target_link_libraries(tinyimx_gateway PUBLIC
    tinyimx_logging
    tinyimx_net
    tinyimx_protocol
    nlohmann_json::nlohmann_json
)

add_executable(gateway_demo
    examples/gateway_demo.cpp
)

target_link_libraries(gateway_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_gateway
)


add_executable(gateway_session_client_demo
    examples/gateway_session_client_demo.cpp
)

target_link_libraries(gateway_session_client_demo PRIVATE
    tinyimx_protocol
)

add_executable(gateway_offline_client_demo
    examples/gateway_offline_client_demo.cpp
)

target_link_libraries(gateway_offline_client_demo PRIVATE
    tinyimx_protocol
)

add_executable(
    gateway_pending_replay_pagination_demo
    examples/gateway_pending_replay_pagination_demo.cpp
)

target_link_libraries(
    gateway_pending_replay_pagination_demo
    PRIVATE
    tinyimx_protocol
)


add_executable(
    gateway_receiver_reconnect_dedup_demo
    examples/gateway_receiver_reconnect_dedup_demo.cpp
)

target_link_libraries(
    gateway_receiver_reconnect_dedup_demo
    PRIVATE
        tinyimx_protocol
)



add_executable(
    gateway_peer_duplicate_client_demo
    examples/gateway_peer_duplicate_client_demo.cpp
)


target_link_libraries(
    gateway_peer_duplicate_client_demo
    PRIVATE
    tinyimx_protocol
    tinyimx_repository
)


add_executable(
    gateway_peer_durable_replay_client_demo
    examples/gateway_peer_durable_replay_client_demo.cpp
)

target_link_libraries(
    gateway_peer_durable_replay_client_demo
    PRIVATE
    tinyimx_protocol
)


add_executable(gateway_tests
    tests/gateway/gateway_test_main.cpp
    tests/gateway/offline_message_store_test.cpp
    tests/gateway/session_manager_test.cpp
    tests/gateway/gateway_peer_transport_test.cpp
    tests/gateway/message_delivery_deduplicator_test.cpp
    tests/gateway/receiver_delivery_tracker_test.cpp
)

target_link_libraries(gateway_tests PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_gateway
)

target_include_directories(gateway_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(run_gateway_tests
    COMMAND $<TARGET_FILE:gateway_tests>
    DEPENDS gateway_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)


# M13: Business Execution Runtime unit tests.
add_executable(business_runtime_tests
    tests/gateway/business_executor_test.cpp
)

target_link_libraries(business_runtime_tests PRIVATE
    tinyimx_gateway
)

target_include_directories(business_runtime_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(run_business_runtime_tests
    COMMAND $<TARGET_FILE:business_runtime_tests>
    DEPENDS business_runtime_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)


# M13-C2: failure-composition and lifecycle acceptance tests.
add_executable(business_runtime_acceptance_tests
    tests/gateway/business_runtime_acceptance_test.cpp
)

target_link_libraries(business_runtime_acceptance_tests PRIVATE
    tinyimx_gateway
)

target_include_directories(business_runtime_acceptance_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

add_custom_target(run_business_runtime_acceptance_tests
    COMMAND $<TARGET_FILE:business_runtime_acceptance_tests>
    DEPENDS business_runtime_acceptance_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)


# M13-C1: isolated Business Runtime capacity/latency benchmark.
add_executable(business_runtime_benchmark
    benchmark/business_runtime_benchmark.cpp
)

target_link_libraries(business_runtime_benchmark PRIVATE
    tinyimx_gateway
)

target_include_directories(business_runtime_benchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

find_program(MYSQL_CONFIG_EXECUTABLE mysql_config REQUIRED)

execute_process(
    COMMAND ${MYSQL_CONFIG_EXECUTABLE} --cflags
    OUTPUT_VARIABLE MYSQL_CFLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND ${MYSQL_CONFIG_EXECUTABLE} --libs
    OUTPUT_VARIABLE MYSQL_LIBS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

separate_arguments(MYSQL_CFLAGS)
separate_arguments(MYSQL_LIBS)

add_library(tinyimx_db
    common/db/MySqlConnection.cpp
    common/db/MySqlConnectionPool.cpp
)

target_include_directories(tinyimx_db PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_db PUBLIC
    cxx_std_20
)

target_compile_options(tinyimx_db PRIVATE
    ${MYSQL_CFLAGS}
)

target_link_libraries(tinyimx_db PUBLIC
    tinyimx_config
    tinyimx_logging
    ${MYSQL_LIBS}
)

add_executable(mysql_pool_demo
    examples/mysql_pool_demo.cpp
)

target_link_libraries(mysql_pool_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_db
)


add_library(tinyimx_repository
    services/repository/UserRepository.cpp
    services/repository/MessageRepository.cpp
    services/repository/FriendRepository.cpp
    services/repository/FriendRequestRepository.cpp
)

target_include_directories(tinyimx_repository PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_repository PUBLIC
    cxx_std_20
)

target_link_libraries(tinyimx_repository PUBLIC
    tinyimx_db
)

add_executable(repository_demo
    examples/repository_demo.cpp
)

target_link_libraries(repository_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_db
    tinyimx_repository
)

add_executable(friend_repository_demo
    examples/friend_repository_demo.cpp
)

target_link_libraries(friend_repository_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_db
    tinyimx_repository
)

add_executable(message_repository_history_demo
    examples/message_repository_history_demo.cpp
)

target_link_libraries(message_repository_history_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_db
    tinyimx_repository
)

add_executable(
    message_repository_client_id_demo
    examples/message_repository_client_id_demo.cpp
)

target_link_libraries(
    message_repository_client_id_demo
    PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_db
    tinyimx_repository
)


add_executable(
    message_repository_idempotent_save_demo
    examples/message_repository_idempotent_save_demo.cpp
)

target_link_libraries(
    message_repository_idempotent_save_demo
    PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_db
    tinyimx_repository
)


add_executable(
    message_repository_idempotent_concurrent_demo
    examples/message_repository_idempotent_concurrent_demo.cpp
)

target_link_libraries(
    message_repository_idempotent_concurrent_demo
    PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_db
    tinyimx_repository
)


target_link_libraries(tinyimx_gateway PUBLIC
    tinyimx_logging
    tinyimx_net
    tinyimx_protocol
    tinyimx_repository
    tinyimx_cache_service
    tinyimx_concurrency
    nlohmann_json::nlohmann_json
)

target_link_libraries(gateway_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_gateway
    tinyimx_db
    tinyimx_repository
    tinyimx_cache
    tinyimx_cache_service
)

find_package(PkgConfig REQUIRED)

pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis)

add_library(tinyimx_cache
    common/cache/RedisConnection.cpp
    common/cache/RedisConnectionPool.cpp
)

target_include_directories(tinyimx_cache PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_cache PUBLIC
    cxx_std_20
)

target_link_libraries(tinyimx_cache PUBLIC
    tinyimx_config
    tinyimx_logging
    PkgConfig::HIREDIS
)

add_executable(redis_pool_demo
    examples/redis_pool_demo.cpp
)

target_link_libraries(redis_pool_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_cache
)


add_library(tinyimx_cache_service
    services/cache/OnlineStatusCache.cpp
    services/cache/UnreadCountCache.cpp
)

target_include_directories(tinyimx_cache_service PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_cache_service PUBLIC
    cxx_std_20
)

target_link_libraries(tinyimx_cache_service PUBLIC
    tinyimx_cache
    nlohmann_json::nlohmann_json
)

add_executable(online_status_cache_demo
    examples/online_status_cache_demo.cpp
)

target_link_libraries(online_status_cache_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_cache
    tinyimx_cache_service
)

add_executable(unread_count_cache_demo
    examples/unread_count_cache_demo.cpp
)

target_link_libraries(unread_count_cache_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_cache
    tinyimx_cache_service
)

find_package(OpenSSL REQUIRED)

target_link_libraries(tinyimx_repository PUBLIC
    tinyimx_db
    tinyimx_logging
    tinyimx_security
)

add_library(tinyimx_security
    common/security/PasswordHasher.cpp
)

target_include_directories(tinyimx_security PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_security PUBLIC
    cxx_std_20
)

target_link_libraries(tinyimx_security PUBLIC
    OpenSSL::Crypto
)

add_executable(password_hasher_demo
    examples/password_hasher_demo.cpp
)

target_link_libraries(password_hasher_demo PRIVATE
    tinyimx_security
)

add_executable(user_password_seed_demo
    examples/user_password_seed_demo.cpp
)

target_link_libraries(user_password_seed_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_db
    tinyimx_security
)

add_executable(user_login_verify_demo
    examples/user_login_verify_demo.cpp
)

target_link_libraries(user_login_verify_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_db
    tinyimx_repository
    tinyimx_security
)


add_executable(gateway_login_auth_client_demo
    examples/gateway_login_auth_client_demo.cpp
)

target_link_libraries(gateway_login_auth_client_demo PRIVATE
    tinyimx_net
    tinyimx_protocol
    nlohmann_json::nlohmann_json
)


add_executable(gateway_duplicate_login_client_demo
    examples/gateway_duplicate_login_client_demo.cpp
)

target_link_libraries(gateway_duplicate_login_client_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_cache
    tinyimx_net
    tinyimx_protocol
    nlohmann_json::nlohmann_json
)


add_executable(gateway_spoof_sender_client_demo
    examples/gateway_spoof_sender_client_demo.cpp
)

target_link_libraries(gateway_spoof_sender_client_demo PRIVATE
    tinyimx_net
    tinyimx_protocol
    nlohmann_json::nlohmann_json
)

add_executable(gateway_relation_permission_client_demo
    examples/gateway_relation_permission_client_demo.cpp
)

target_link_libraries(gateway_relation_permission_client_demo PRIVATE
    tinyimx_net
    tinyimx_protocol
    nlohmann_json::nlohmann_json
)

add_executable(gateway_legacy_login_client_demo
    examples/gateway_legacy_login_client_demo.cpp
)

target_link_libraries(gateway_legacy_login_client_demo PRIVATE
    tinyimx_net
    tinyimx_protocol
    nlohmann_json::nlohmann_json
)

add_executable(gateway_history_client_demo
    examples/gateway_history_client_demo.cpp
)

target_link_libraries(gateway_history_client_demo PRIVATE
    tinyimx_net
    tinyimx_protocol
    nlohmann_json::nlohmann_json
)


add_executable(
    gateway_business_executor_isolation_demo
    examples/gateway_business_executor_isolation_demo.cpp
)

target_link_libraries(
    gateway_business_executor_isolation_demo
    PRIVATE
    tinyimx_net
    tinyimx_protocol
    nlohmann_json::nlohmann_json
)


# M13-C2: real Gateway overload/load-shedding integration client.
add_executable(
    gateway_business_runtime_overload_demo
    examples/gateway_business_runtime_overload_demo.cpp
)

target_link_libraries(
    gateway_business_runtime_overload_demo
    PRIVATE
    tinyimx_net
    tinyimx_protocol
    nlohmann_json::nlohmann_json
)


add_executable(message_repository_conversation_demo
    examples/message_repository_conversation_demo.cpp
)

target_link_libraries(message_repository_conversation_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_db
    tinyimx_repository
)

add_executable(gateway_conversation_client_demo
    examples/gateway_conversation_client_demo.cpp
)

target_link_libraries(gateway_conversation_client_demo PRIVATE
    tinyimx_net
    tinyimx_protocol
    nlohmann_json::nlohmann_json
)

add_executable(friend_repository_list_demo
    examples/friend_repository_list_demo.cpp
)

target_link_libraries(friend_repository_list_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_db
    tinyimx_repository
)

add_executable(gateway_friend_list_client_demo
    examples/gateway_friend_list_client_demo.cpp
)

target_link_libraries(gateway_friend_list_client_demo PRIVATE
    tinyimx_net
    tinyimx_protocol
    nlohmann_json::nlohmann_json
)

add_executable(gateway_friend_request_client_demo
    examples/gateway_friend_request_client_demo.cpp
)

target_link_libraries(gateway_friend_request_client_demo PRIVATE
    tinyimx_net
    tinyimx_protocol
    nlohmann_json::nlohmann_json
)

add_executable(
    gateway_friend_request_reject_client_demo
    examples/gateway_friend_request_reject_client_demo.cpp
)

target_link_libraries(
    gateway_friend_request_reject_client_demo
    PRIVATE
        tinyimx_net
        tinyimx_protocol
        nlohmann_json::nlohmann_json
)

add_executable(mysql_transaction_demo
    examples/mysql_transaction_demo.cpp
)

target_link_libraries(mysql_transaction_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_db
)

add_executable(friend_request_create_demo
    examples/friend_request_create_demo.cpp
)

target_link_libraries(friend_request_create_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_db
    tinyimx_repository
)

add_executable(friend_request_list_demo
    examples/friend_request_list_demo.cpp
)

target_link_libraries(friend_request_list_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_db
    tinyimx_repository
)

add_executable(friend_request_accept_demo
    examples/friend_request_accept_demo.cpp
)

target_link_libraries(friend_request_accept_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_db
    tinyimx_repository
)

add_executable(friend_request_reject_demo
    examples/friend_request_reject_demo.cpp
)

target_link_libraries(friend_request_reject_demo PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_db
    tinyimx_repository
)

if(BUILD_TESTING)
    add_test(
        NAME tinyimx.config.business_runtime
        COMMAND config_business_runtime_tests
    )

    add_test(
        NAME tinyimx.business_runtime
        COMMAND business_runtime_tests
    )

    add_test(
        NAME tinyimx.business_runtime.acceptance
        COMMAND business_runtime_acceptance_tests
    )

    add_test(
        NAME tinyimx.concurrency
        COMMAND concurrency_tests
    )

    add_test(
        NAME tinyimx.net
        COMMAND net_tests
    )

    add_test(
        NAME tinyimx.protocol
        COMMAND protocol_tests
    )

    add_test(
        NAME tinyimx.gateway
        COMMAND gateway_tests
    )
endif()

add_library(tinyimx_registry
    services/registry/GatewayRegistry.cpp
    services/registry/GatewayRegistryLease.cpp
    services/registry/GatewayDiscovery.cpp
)

target_include_directories(tinyimx_registry PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_registry PUBLIC
    cxx_std_20
)

target_link_libraries(tinyimx_registry PUBLIC
    tinyimx_cache
    tinyimx_logging
    nlohmann_json::nlohmann_json
)

target_link_libraries(
    tinyimx_gateway
    PUBLIC
    tinyimx_registry
)

target_link_libraries(
    gateway_demo
    PRIVATE
    tinyimx_registry
)

add_executable(
    gateway_registry_demo
    examples/gateway_registry_demo.cpp
)

target_link_libraries(
    gateway_registry_demo
    PRIVATE
    tinyimx_registry
    tinyimx_gateway
    tinyimx_config
    tinyimx_logging
)


add_executable(
    gateway_same_gateway_client_demo
    examples/gateway_same_gateway_client_demo.cpp
)

target_link_libraries(
    gateway_same_gateway_client_demo
    PRIVATE
    tinyimx_protocol
)


add_executable(
    gateway_client_retry_demo
    examples/gateway_client_retry_demo.cpp
)

target_link_libraries(
    gateway_client_retry_demo
    PRIVATE
    tinyimx_protocol
)

add_executable(
    gateway_client_ack_loss_demo
    examples/gateway_client_ack_loss_demo.cpp
)

target_link_libraries(
    gateway_client_ack_loss_demo
    PRIVATE
    tinyimx_protocol
)


add_executable(
    gateway_client_retry_exhaustion_demo
    examples/gateway_client_retry_exhaustion_demo.cpp
)

target_link_libraries(
    gateway_client_retry_exhaustion_demo
    PRIVATE
    tinyimx_protocol
)


add_executable(
    gateway_offline_idempotency_client_demo
    examples/gateway_offline_idempotency_client_demo.cpp
)

target_link_libraries(
    gateway_offline_idempotency_client_demo
    PRIVATE
    tinyimx_protocol
)


add_executable(
    gateway_offline_recovery_client_demo
    examples/gateway_offline_recovery_client_demo.cpp
)

target_link_libraries(
    gateway_offline_recovery_client_demo
    PRIVATE
    tinyimx_protocol
)


add_executable(
    gateway_cross_gateway_client_demo
    examples/gateway_cross_gateway_client_demo.cpp
)

target_link_libraries(
    gateway_cross_gateway_client_demo
    PRIVATE
    tinyimx_protocol
)

add_executable(
    gateway_cross_gateway_reconnect_dedup_demo
    examples/gateway_cross_gateway_reconnect_dedup_demo.cpp
)

target_link_libraries(
    gateway_cross_gateway_reconnect_dedup_demo
    PRIVATE
        tinyimx_protocol
)

add_executable(
    gateway_cross_gateway_gateway_b_crash_recovery_demo
    examples/gateway_cross_gateway_gateway_b_crash_recovery_demo.cpp
)

target_link_libraries(
    gateway_cross_gateway_gateway_b_crash_recovery_demo
    PRIVATE
        tinyimx_protocol
)

add_executable(
    gateway_cross_gateway_peer_response_loss_demo
    examples/gateway_cross_gateway_peer_response_loss_demo.cpp
)

target_link_libraries(
    gateway_cross_gateway_peer_response_loss_demo
    PRIVATE
        tinyimx_protocol
)
