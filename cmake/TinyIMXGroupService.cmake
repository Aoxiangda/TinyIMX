include_guard(GLOBAL)

foreach(required_target IN ITEMS
    tinyimx_rpc_proto
    tinyimx_repository
    tinyimx_service_registry
    tinyimx_outbox
)
  if(NOT TARGET ${required_target})
    message(FATAL_ERROR
      "TinyIMXGroupService.cmake requires target: ${required_target}. "
      "Include this module after TinyIMXMessageService.cmake in root CMakeLists.txt.")
  endif()
endforeach()

add_library(tinyimx_group_core
  services/group/application/GroupApplicationService.cpp
  services/group/application/GroupPermissionPolicy.cpp
  services/group/application/GroupEventFactory.cpp
  services/group/repository/GroupRepositoryAdapter.cpp
  services/group/repository/GroupMembershipRepositoryAdapter.cpp
)

target_include_directories(tinyimx_group_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_group_core PUBLIC cxx_std_20)

target_link_libraries(tinyimx_group_core PUBLIC
  tinyimx_repository
  tinyimx_outbox
  OpenSSL::Crypto
)

add_library(tinyimx_group_grpc
  services/group/service/GroupServiceImpl.cpp
  services/group/server/GroupServiceServer.cpp
)

target_include_directories(tinyimx_group_grpc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_group_grpc PUBLIC cxx_std_20)

target_link_libraries(tinyimx_group_grpc PUBLIC
  tinyimx_group_core
  tinyimx_rpc_proto
  gRPC::grpc++
)

add_executable(group_service_demo
  examples/group_service_demo.cpp
)

target_compile_features(group_service_demo PRIVATE cxx_std_20)

target_link_libraries(group_service_demo PRIVATE
  tinyimx_config
  tinyimx_logging
  tinyimx_db
  tinyimx_repository
  tinyimx_group_grpc
  tinyimx_service_registry
  tinyimx_outbox
)

add_executable(m17_a1_rocketmq_subscription_contract_tests
  tests/eventing/m17_a1_rocketmq_subscription_contract_test.cpp
)

target_compile_features(m17_a1_rocketmq_subscription_contract_tests PRIVATE cxx_std_20)
target_include_directories(m17_a1_rocketmq_subscription_contract_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)
add_test(
  NAME m17_a1_rocketmq_subscription_contract_tests
  COMMAND m17_a1_rocketmq_subscription_contract_tests
)

add_executable(group_permission_policy_tests
  tests/group/group_permission_policy_test.cpp
)

target_compile_features(group_permission_policy_tests PRIVATE cxx_std_20)
target_link_libraries(group_permission_policy_tests PRIVATE tinyimx_group_core)
add_test(NAME group_permission_policy_tests COMMAND group_permission_policy_tests)

add_executable(group_application_service_tests
  tests/group/group_application_service_test.cpp
)

target_compile_features(group_application_service_tests PRIVATE cxx_std_20)
target_link_libraries(group_application_service_tests PRIVATE tinyimx_group_core)
add_test(NAME group_application_service_tests COMMAND group_application_service_tests)

add_executable(group_repository_integration_tests
  tests/group/group_repository_integration_test.cpp
)

target_compile_features(group_repository_integration_tests PRIVATE cxx_std_20)
target_link_libraries(group_repository_integration_tests PRIVATE
  tinyimx_config
  tinyimx_logging
  tinyimx_db
  tinyimx_repository
  tinyimx_group_core
  tinyimx_outbox
)

# External-MySQL test: intentionally not added to ordinary CTest. Run it
# explicitly in the M17-A1 acceptance workflow after migration 006.


add_executable(group_membership_repository_integration_tests
  tests/group/group_membership_repository_integration_test.cpp
)

target_compile_features(group_membership_repository_integration_tests PRIVATE cxx_std_20)
target_link_libraries(group_membership_repository_integration_tests PRIVATE
  tinyimx_config
  tinyimx_logging
  tinyimx_db
  tinyimx_repository
  tinyimx_group_core
  tinyimx_outbox
)

# External-MySQL test: run only from the explicit M17-A2 acceptance workflow.

add_executable(group_service_integration_tests
  tests/group/group_service_integration_test.cpp
)

target_compile_features(group_service_integration_tests PRIVATE cxx_std_20)
target_link_libraries(group_service_integration_tests PRIVATE
  tinyimx_group_grpc
  tinyimx_rpc_proto
  gRPC::grpc++
)
add_test(NAME group_service_integration_tests COMMAND group_service_integration_tests)
