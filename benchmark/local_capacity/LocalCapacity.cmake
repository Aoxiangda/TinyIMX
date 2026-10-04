# Additive diagnostics target; does not replace frozen loadgens or Gateway code.
find_path(TINYIMX_CAPACITY_BOOST_HEADERS boost/property_tree/json_parser.hpp)
if(NOT TINYIMX_CAPACITY_BOOST_HEADERS)
  message(FATAL_ERROR "Existing boost-property-tree headers missing. STOP; do not download dependencies.")
endif()
add_executable(tinyimx_capacity_worker EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_LIST_DIR}/capacity_worker.cpp
    ${PROJECT_SOURCE_DIR}/common/net/Buffer.cpp
    ${PROJECT_SOURCE_DIR}/common/protocol/Packet.cpp
    ${PROJECT_SOURCE_DIR}/common/protocol/ProtocolCodec.cpp)
target_include_directories(tinyimx_capacity_worker PRIVATE ${PROJECT_SOURCE_DIR} ${TINYIMX_CAPACITY_BOOST_HEADERS})
target_compile_features(tinyimx_capacity_worker PRIVATE cxx_std_17)
target_compile_definitions(tinyimx_capacity_worker PRIVATE BOOST_BIND_GLOBAL_PLACEHOLDERS)

# Explicit live-MySQL regression; all fixtures are connection-local temporary
# shadows. Not in the default external-resource-free CTest suite.
add_executable(unread_snapshot_aggregate_tests EXCLUDE_FROM_ALL
    ${PROJECT_SOURCE_DIR}/tests/projection/unread_snapshot_aggregate_test.cpp)
target_compile_features(unread_snapshot_aggregate_tests PRIVATE cxx_std_20)
target_link_libraries(unread_snapshot_aggregate_tests PRIVATE
    tinyimx_config tinyimx_logging tinyimx_db tinyimx_unread_projection)

# Explicit real-resource probe through owned loopback relays; never default CTest.
add_executable(pool_recovery_probe EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_LIST_DIR}/pool_recovery_probe.cpp)
target_compile_features(pool_recovery_probe PRIVATE cxx_std_20)
target_link_libraries(pool_recovery_probe PRIVATE tinyimx_config tinyimx_db tinyimx_cache)

add_executable(private_persistence_trace_tests EXCLUDE_FROM_ALL
    ${PROJECT_SOURCE_DIR}/tests/message/private_persistence_trace_test.cpp)
target_compile_features(private_persistence_trace_tests PRIVATE cxx_std_20)
target_link_libraries(private_persistence_trace_tests PRIVATE tinyimx_logging)
