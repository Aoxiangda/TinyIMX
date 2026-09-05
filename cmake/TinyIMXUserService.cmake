include_guard(GLOBAL)

foreach(required_target IN ITEMS
    tinyimx_rpc_proto
    tinyimx_rpc_client
    tinyimx_repository
)
  if(NOT TARGET ${required_target})
    message(FATAL_ERROR
      "TinyIMXUserService.cmake requires target: ${required_target}. "
      "Include this module near the end of the root CMakeLists.txt.")
  endif()
endforeach()

add_library(tinyimx_user_core
  services/user/application/UserApplicationService.cpp
  services/user/repository/UserRepositoryAdapter.cpp
)

target_include_directories(tinyimx_user_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_user_core PUBLIC cxx_std_20)

target_link_libraries(tinyimx_user_core PUBLIC
  tinyimx_repository
)

add_library(tinyimx_user_grpc
  services/user/service/UserServiceImpl.cpp
  services/user/server/UserServiceServer.cpp
)

target_include_directories(tinyimx_user_grpc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_user_grpc PUBLIC cxx_std_20)

target_link_libraries(tinyimx_user_grpc PUBLIC
  tinyimx_user_core
  tinyimx_rpc_proto
  gRPC::grpc++
)

add_executable(user_service_demo
  examples/user_service_demo.cpp
)

target_compile_features(user_service_demo PRIVATE cxx_std_20)

target_link_libraries(user_service_demo PRIVATE
  tinyimx_config
  tinyimx_logging
  tinyimx_db
  tinyimx_repository
  tinyimx_user_grpc
)

add_executable(user_application_service_tests
  tests/user/user_application_service_test.cpp
)

target_compile_features(user_application_service_tests PRIVATE cxx_std_20)

target_link_libraries(user_application_service_tests PRIVATE
  tinyimx_user_core
)

add_test(
  NAME user_application_service_tests
  COMMAND user_application_service_tests
)

add_executable(user_service_integration_tests
  tests/user/user_service_integration_test.cpp
)

target_compile_features(user_service_integration_tests PRIVATE cxx_std_20)

target_link_libraries(user_service_integration_tests PRIVATE
  tinyimx_user_grpc
  tinyimx_rpc_client
)

add_test(
  NAME user_service_integration_tests
  COMMAND user_service_integration_tests
)
