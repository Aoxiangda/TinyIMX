# Narrow component/edge-double tests; not a replacement for gateway_tests/E2E.
find_package(Threads REQUIRED)
add_executable(private_chat_ack_boundary_tests EXCLUDE_FROM_ALL
    "${PROJECT_SOURCE_DIR}/tests/gateway/private_chat_ack_boundary_test.cpp")
target_include_directories(private_chat_ack_boundary_tests PRIVATE "${PROJECT_SOURCE_DIR}")
target_compile_features(private_chat_ack_boundary_tests PRIVATE cxx_std_20)
target_link_libraries(private_chat_ack_boundary_tests PRIVATE Threads::Threads)
