include_guard(GLOBAL)

foreach(required_target IN ITEMS
    tinyimx_rpc_proto
    tinyimx_repository
)
  if(NOT TARGET ${required_target})
    message(FATAL_ERROR
      "TinyIMXFileService.cmake requires target: ${required_target}. "
      "Include this module near the end of root CMakeLists.txt.")
  endif()
endforeach()

add_library(tinyimx_file_core
  services/file/application/FileApplicationService.cpp
  services/file/repository/FileRepositoryAdapter.cpp
)

target_include_directories(tinyimx_file_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_file_core PUBLIC cxx_std_20)

target_link_libraries(tinyimx_file_core PUBLIC
  tinyimx_repository
  OpenSSL::Crypto
)

add_library(tinyimx_file_grpc
  services/file/service/FileServiceImpl.cpp
  services/file/server/FileServiceServer.cpp
)

target_include_directories(tinyimx_file_grpc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_file_grpc PUBLIC cxx_std_20)

target_link_libraries(tinyimx_file_grpc PUBLIC
  tinyimx_file_core
  tinyimx_rpc_proto
  gRPC::grpc++
)

add_executable(file_application_service_tests
  tests/file/file_application_service_test.cpp
)

target_compile_features(file_application_service_tests PRIVATE cxx_std_20)
target_link_libraries(file_application_service_tests PRIVATE tinyimx_file_core)
add_test(NAME file_application_service_tests COMMAND file_application_service_tests)

add_executable(file_service_integration_tests
  tests/file/file_service_integration_test.cpp
)

target_compile_features(file_service_integration_tests PRIVATE cxx_std_20)
target_link_libraries(file_service_integration_tests PRIVATE
  tinyimx_file_grpc
  tinyimx_rpc_proto
  gRPC::grpc++
)
add_test(NAME file_service_integration_tests COMMAND file_service_integration_tests)

add_executable(file_repository_integration_tests
  tests/file/file_repository_integration_test.cpp
)

target_compile_features(file_repository_integration_tests PRIVATE cxx_std_20)
target_link_libraries(file_repository_integration_tests PRIVATE
  tinyimx_config
  tinyimx_logging
  tinyimx_db
  tinyimx_repository
  tinyimx_file_core
)

# External-MySQL test: intentionally not added to ordinary CTest. Apply
# migration 009 and run explicitly in the M18-A1 acceptance workflow.
