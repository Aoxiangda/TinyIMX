add_executable(business_exact_key_tests tests/gateway/exact_order_key_test.cpp)
target_link_libraries(business_exact_key_tests PRIVATE tinyimx_gateway)
target_include_directories(business_exact_key_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME business_exact_key_tests COMMAND business_exact_key_tests)

# Compile the changed executor itself under sanitizers as well as the fixture.
# Other linked libraries retain their normal instrumentation/build settings.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_executable(business_exact_key_sanitizer_tests
        tests/gateway/exact_order_key_test.cpp gateway/business/BusinessExecutor.cpp)
    target_link_libraries(business_exact_key_sanitizer_tests PRIVATE tinyimx_concurrency)
    target_include_directories(business_exact_key_sanitizer_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(business_exact_key_sanitizer_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(business_exact_key_sanitizer_tests PRIVATE -fsanitize=address,undefined)
    add_test(NAME business_exact_key_sanitizer_tests COMMAND business_exact_key_sanitizer_tests)
endif()
