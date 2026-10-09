add_executable(rpc_readiness_probe examples/rpc_readiness_probe.cpp)
target_include_directories(rpc_readiness_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rpc_readiness_probe PRIVATE gRPC::grpc++)
add_executable(rpc_readiness_tests tests/rpc/rpc_readiness_test.cpp)
target_include_directories(rpc_readiness_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(rpc_readiness_tests PRIVATE tinyimx_user_grpc
    tinyimx_social_grpc tinyimx_message_grpc tinyimx_group_grpc tinyimx_file_grpc)
add_test(NAME rpc_readiness_tests COMMAND rpc_readiness_tests)
set_tests_properties(rpc_readiness_tests PROPERTIES LABELS "unit;rpc;readiness")
