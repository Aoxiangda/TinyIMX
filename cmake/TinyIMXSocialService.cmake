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
      "TinyIMXSocialService.cmake requires target: ${required_target}. "
      "Include this module near the end of the root CMakeLists.txt.")
  endif()
endforeach()

add_library(tinyimx_social_core
  services/social/application/FriendApplicationService.cpp
  services/social/repository/FriendRepositoryAdapter.cpp
)

target_include_directories(tinyimx_social_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_social_core PUBLIC cxx_std_20)

target_link_libraries(tinyimx_social_core PUBLIC
  tinyimx_repository
)

add_library(tinyimx_social_grpc
  services/social/service/SocialServiceImpl.cpp
  services/social/server/SocialServiceServer.cpp
)

target_include_directories(tinyimx_social_grpc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_social_grpc PUBLIC cxx_std_20)

target_link_libraries(tinyimx_social_grpc PUBLIC
  tinyimx_social_core
  tinyimx_rpc_proto
  gRPC::grpc++
)

add_executable(social_service_demo
  examples/social_service_demo.cpp
)

target_compile_features(social_service_demo PRIVATE cxx_std_20)

target_link_libraries(social_service_demo PRIVATE
  tinyimx_config
  tinyimx_logging
  tinyimx_db
  tinyimx_repository
  tinyimx_social_grpc
)

add_executable(friend_application_service_tests
  tests/social/friend_application_service_test.cpp
)

target_compile_features(friend_application_service_tests PRIVATE cxx_std_20)

target_link_libraries(friend_application_service_tests PRIVATE
  tinyimx_social_core
)

add_test(
  NAME friend_application_service_tests
  COMMAND friend_application_service_tests
)

add_executable(social_service_integration_tests
  tests/social/social_service_integration_test.cpp
)

target_compile_features(social_service_integration_tests PRIVATE cxx_std_20)

target_link_libraries(social_service_integration_tests PRIVATE
  tinyimx_social_grpc
  tinyimx_rpc_client
)

add_test(
  NAME social_service_integration_tests
  COMMAND social_service_integration_tests
)

# A3 is the first production path that lets Gateway call SocialService.
# The dependency is intentionally added only now, not in A2.
target_link_libraries(tinyimx_gateway PUBLIC
  tinyimx_rpc_client
)

target_link_libraries(gateway_demo PRIVATE
  tinyimx_rpc_client
)
