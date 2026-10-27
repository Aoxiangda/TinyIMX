add_executable(bounded_async_ack_batch_tests tests/eventing/bounded_async_ack_batch_test.cpp)
target_include_directories(bounded_async_ack_batch_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bounded_async_ack_batch_tests PRIVATE Threads::Threads)
add_test(NAME bounded_async_ack_batch_tests COMMAND bounded_async_ack_batch_tests)

if(TARGET tinyimx_rocketmq_transport)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(rocketmq_wire_ack_capability_tests
            tests/eventing/rocketmq_wire_ack_capability_test.cpp)
        target_link_libraries(rocketmq_wire_ack_capability_tests PRIVATE
            tinyimx_rocketmq_transport)
        add_test(NAME rocketmq_wire_ack_capability_tests COMMAND rocketmq_wire_ack_capability_tests)
    endif()
    # Requires exact owned r27 scope and an explicit flag, never a default ctest.
    add_executable(rocketmq_ack_batch_integration_tests
        tests/eventing/rocketmq_ack_batch_integration_test.cpp)
    target_link_libraries(rocketmq_ack_batch_integration_tests PRIVATE
        tinyimx_config tinyimx_logging tinyimx_rocketmq_transport)
    set_target_properties(rocketmq_ack_batch_integration_tests PROPERTIES
        BUILD_RPATH "${_tinyimx_rocketmq_runtime_paths}")
    add_executable(unread_batch_ack_contract_tests
        tests/projection/unread_batch_ack_contract_test.cpp)
    target_link_libraries(unread_batch_ack_contract_tests PRIVATE
        tinyimx_config tinyimx_logging tinyimx_db tinyimx_cache
        tinyimx_cache_service tinyimx_unread_projection)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET tinyimx_unread_projection)
    add_executable(unread_pair_snapshot_tests EXCLUDE_FROM_ALL
        tests/projection/unread_pair_snapshot_test.cpp)
    target_link_libraries(unread_pair_snapshot_tests PRIVATE tinyimx_config
        tinyimx_logging tinyimx_cache tinyimx_cache_service)
    # Explicit owned-cache/read-only SQL flags required; never default ctest.
    foreach(_snapshot_test IN ITEMS unread_snapshot_batch_contract_tests unread_snapshot_batch_sql_tests)
        if(_snapshot_test STREQUAL "unread_snapshot_batch_contract_tests")
            set(_snapshot_source tests/projection/unread_snapshot_batch_contract_test.cpp)
        else()
            set(_snapshot_source tests/projection/unread_snapshot_batch_sql_test.cpp)
        endif()
        add_executable(${_snapshot_test} ${_snapshot_source})
        target_link_libraries(${_snapshot_test} PRIVATE tinyimx_config tinyimx_logging
            tinyimx_db tinyimx_cache tinyimx_cache_service tinyimx_unread_projection)
    endforeach()
endif()
