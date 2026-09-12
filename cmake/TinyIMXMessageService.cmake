include_guard(GLOBAL)

foreach(required_target IN ITEMS
    tinyimx_rpc_proto
    tinyimx_rpc_client
    tinyimx_repository
    tinyimx_service_registry
    tinyimx_gateway
    gateway_demo
)
  if(NOT TARGET ${required_target})
    message(FATAL_ERROR
      "TinyIMXMessageService.cmake requires target: ${required_target}. "
      "Include this module near the end of the root CMakeLists.txt.")
  endif()
endforeach()

add_library(tinyimx_eventing
  services/eventing/DomainEvent.cpp
  services/eventing/EventCodec.cpp
  services/eventing/EventPublisher.cpp
)

target_include_directories(tinyimx_eventing PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_eventing PUBLIC cxx_std_20)

target_link_libraries(tinyimx_eventing PUBLIC
  nlohmann_json::nlohmann_json
)

add_library(tinyimx_outbox
  services/outbox/OutboxRepository.cpp
)

target_include_directories(tinyimx_outbox PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_outbox PUBLIC cxx_std_20)

target_link_libraries(tinyimx_outbox PUBLIC
  tinyimx_db
  tinyimx_eventing
)

add_library(tinyimx_outbox_relay
  services/outbox/OutboxRelay.cpp
)

target_include_directories(tinyimx_outbox_relay PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_outbox_relay PUBLIC cxx_std_20)

target_link_libraries(tinyimx_outbox_relay PUBLIC
  tinyimx_outbox
  tinyimx_eventing
  tinyimx_logging
)

add_library(tinyimx_unread_projection
  services/projection/unread/UnreadProjectionReader.cpp
  services/projection/unread/UnreadProjector.cpp
)

target_include_directories(tinyimx_unread_projection PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_unread_projection PUBLIC cxx_std_20)

target_link_libraries(tinyimx_unread_projection PUBLIC
  tinyimx_db
  tinyimx_cache_service
  tinyimx_eventing
  tinyimx_logging
)

add_library(tinyimx_message_core
  services/message/application/MessageApplicationService.cpp
  services/message/application/MessageEventFactory.cpp
  services/message/repository/MessageRepositoryAdapter.cpp
)

target_include_directories(tinyimx_message_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_message_core PUBLIC cxx_std_20)

target_link_libraries(tinyimx_message_core PUBLIC
  tinyimx_repository
  tinyimx_outbox
)

add_library(tinyimx_message_grpc
  services/message/service/MessageServiceImpl.cpp
  services/message/server/MessageServiceServer.cpp
)

target_include_directories(tinyimx_message_grpc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_message_grpc PUBLIC cxx_std_20)

target_link_libraries(tinyimx_message_grpc PUBLIC
  tinyimx_message_core
  tinyimx_rpc_proto
  gRPC::grpc++
)

add_executable(message_service_demo
  examples/message_service_demo.cpp
)

target_compile_features(message_service_demo PRIVATE cxx_std_20)

target_link_libraries(message_service_demo PRIVATE
  tinyimx_config
  tinyimx_logging
  tinyimx_db
  tinyimx_repository
  tinyimx_message_grpc
  tinyimx_service_registry
)

add_executable(message_application_service_tests
  tests/message/message_application_service_test.cpp
)

target_compile_features(message_application_service_tests PRIVATE cxx_std_20)

target_link_libraries(message_application_service_tests PRIVATE
  tinyimx_message_core
)

add_test(
  NAME message_application_service_tests
  COMMAND message_application_service_tests
)

add_executable(config_m16_b_tests
  tests/config/config_m16_b_test.cpp
)

target_compile_features(config_m16_b_tests PRIVATE cxx_std_20)
target_link_libraries(config_m16_b_tests PRIVATE tinyimx_config)
add_test(NAME config_m16_b_tests COMMAND config_m16_b_tests)

add_executable(outbox_relay_unit_tests
  tests/outbox/outbox_relay_unit_test.cpp
)

target_compile_features(outbox_relay_unit_tests PRIVATE cxx_std_20)
target_link_libraries(outbox_relay_unit_tests PRIVATE
  tinyimx_outbox_relay
  tinyimx_eventing
)
add_test(NAME outbox_relay_unit_tests COMMAND outbox_relay_unit_tests)

# ---------------------------------------------------------------------------
# M16-C deterministic crash-window contract tests.
#
# These tests validate exact crash boundaries using fork/_exit and dependency
# decorators. They do not claim real RocketMQ/MySQL/Redis recovery; runtime
# acceptance is executed separately.
# ---------------------------------------------------------------------------
add_executable(m16_crash_window_contract_tests
    ${CMAKE_SOURCE_DIR}/tests/reliability/m16_crash_window_contract_test.cpp
)

target_compile_features(
    m16_crash_window_contract_tests
    PRIVATE
        cxx_std_20
)

target_include_directories(
    m16_crash_window_contract_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(
    m16_crash_window_contract_tests
    PRIVATE
        tinyimx_eventing
        tinyimx_outbox_relay
)

add_test(
    NAME m16_crash_window_contract_tests
    COMMAND m16_crash_window_contract_tests
)


add_executable(unread_projection_reader_integration_tests
  tests/projection/unread_projection_reader_integration_test.cpp
)
target_compile_features(unread_projection_reader_integration_tests PRIVATE cxx_std_20)
target_link_libraries(unread_projection_reader_integration_tests PRIVATE
  tinyimx_config
  tinyimx_logging
  tinyimx_db
  tinyimx_unread_projection
)

add_executable(unread_projection_cache_integration_tests
  tests/projection/unread_projection_cache_integration_test.cpp
)
target_compile_features(unread_projection_cache_integration_tests PRIVATE cxx_std_20)
target_link_libraries(unread_projection_cache_integration_tests PRIVATE
  tinyimx_config
  tinyimx_logging
  tinyimx_cache
  tinyimx_cache_service
)

add_executable(message_outbox_integration_tests
  tests/outbox/message_outbox_integration_test.cpp
)

target_compile_features(message_outbox_integration_tests PRIVATE cxx_std_20)

target_link_libraries(message_outbox_integration_tests PRIVATE
  tinyimx_config
  tinyimx_logging
  tinyimx_db
  tinyimx_repository
  tinyimx_message_core
  tinyimx_outbox
)

add_executable(message_service_integration_tests
  tests/message/message_service_integration_test.cpp
)

target_compile_features(message_service_integration_tests PRIVATE cxx_std_20)

target_link_libraries(message_service_integration_tests PRIVATE
  tinyimx_message_grpc
  tinyimx_rpc_client
)

add_test(
  NAME message_service_integration_tests
  COMMAND message_service_integration_tests
)

add_executable(gateway_message_read_case_client_demo
  examples/gateway_message_read_case_client_demo.cpp
)

target_compile_features(gateway_message_read_case_client_demo PRIVATE cxx_std_20)

target_link_libraries(gateway_message_read_case_client_demo PRIVATE
  tinyimx_net
  tinyimx_protocol
  nlohmann_json::nlohmann_json
)

# C1 makes only History/ConversationList use MessageRpcClient. Other reliable
# message paths still use the local MessageRepository until C2/C3.
target_link_libraries(tinyimx_gateway PUBLIC
  tinyimx_rpc_client
)

target_link_libraries(gateway_demo PRIVATE
  tinyimx_rpc_client
)
