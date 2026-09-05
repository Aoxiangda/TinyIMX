include_guard(GLOBAL)

foreach(required_target IN ITEMS
    tinyimx_rpc_proto
    tinyimx_rpc_client
    tinyimx_repository
    tinyimx_gateway
    gateway_demo
)
  if(NOT TARGET ${required_target})
    message(FATAL_ERROR
      "TinyIMXMessageService.cmake requires target: ${required_target}. "
      "Include this module near the end of the root CMakeLists.txt.")
  endif()
endforeach()

add_library(tinyimx_message_core
  services/message/application/MessageApplicationService.cpp
  services/message/repository/MessageRepositoryAdapter.cpp
)

target_include_directories(tinyimx_message_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_message_core PUBLIC cxx_std_20)

target_link_libraries(tinyimx_message_core PUBLIC
  tinyimx_repository
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
