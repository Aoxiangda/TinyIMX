include_guard(GLOBAL)

foreach(required_target IN ITEMS
    tinyimx_config
    tinyimx_logging
)
  if(NOT TARGET ${required_target})
    message(FATAL_ERROR
      "TinyIMXZooKeeperRegistry.cmake requires target: ${required_target}")
  endif()
endforeach()

add_library(tinyimx_service_registry
  services/registry/zookeeper/ZooKeeperTypes.cpp
  services/registry/zookeeper/ServiceInstance.cpp
  services/registry/zookeeper/ZooKeeperClient.cpp
  services/registry/zookeeper/ZooKeeperServiceRegistrar.cpp
  services/registry/zookeeper/ZooKeeperServiceDiscovery.cpp
)

target_include_directories(tinyimx_service_registry PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_service_registry PUBLIC cxx_std_20)

# ZooKeeper's multithreaded C binding exposes the synchronous API when
# THREADED is defined. The vcpkg "sync" feature links the matching mt library.
target_compile_definitions(tinyimx_service_registry PRIVATE THREADED)

target_link_libraries(tinyimx_service_registry PUBLIC
  tinyimx_config
  tinyimx_logging
  nlohmann_json::nlohmann_json
  unofficial::zookeeper::zookeeper
)

add_executable(config_zookeeper_tests
  tests/config/config_zookeeper_test.cpp
)

target_compile_features(config_zookeeper_tests PRIVATE cxx_std_20)
target_link_libraries(config_zookeeper_tests PRIVATE tinyimx_config)

add_test(
  NAME config_zookeeper_tests
  COMMAND config_zookeeper_tests
)

add_executable(service_instance_tests
  tests/registry/service_instance_test.cpp
)

target_compile_features(service_instance_tests PRIVATE cxx_std_20)
target_link_libraries(service_instance_tests PRIVATE tinyimx_service_registry)

add_test(
  NAME service_instance_tests
  COMMAND service_instance_tests
)

add_executable(zookeeper_service_registry_integration_tests
  tests/registry/zookeeper_service_registry_integration_test.cpp
)

target_compile_features(
  zookeeper_service_registry_integration_tests
  PRIVATE cxx_std_20
)

target_link_libraries(zookeeper_service_registry_integration_tests PRIVATE
  tinyimx_service_registry
)

add_executable(zookeeper_registry_probe
  examples/zookeeper_registry_probe.cpp
)

target_compile_features(zookeeper_registry_probe PRIVATE cxx_std_20)
target_link_libraries(zookeeper_registry_probe PRIVATE
  tinyimx_service_registry
)


# M15-B: ZooKeeper-backed endpoint adapter stays separate from the generic RPC
# client library so User/Social/Message clients remain registry-agnostic.
add_library(tinyimx_zookeeper_endpoint_provider
  services/rpc/ZooKeeperServiceEndpointProvider.cpp
)

target_include_directories(tinyimx_zookeeper_endpoint_provider PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_zookeeper_endpoint_provider PUBLIC cxx_std_20)

target_link_libraries(tinyimx_zookeeper_endpoint_provider PUBLIC
  tinyimx_service_registry
)

if(TARGET gateway_demo)
  target_link_libraries(gateway_demo PRIVATE
    tinyimx_service_registry
    tinyimx_zookeeper_endpoint_provider
  )
endif()

add_executable(config_service_discovery_tests
  tests/config/config_service_discovery_test.cpp
)

target_compile_features(config_service_discovery_tests PRIVATE cxx_std_20)
target_link_libraries(config_service_discovery_tests PRIVATE tinyimx_config)

add_test(
  NAME config_service_discovery_tests
  COMMAND config_service_discovery_tests
)

add_executable(zookeeper_service_discovery_integration_tests
  tests/registry/zookeeper_service_discovery_integration_test.cpp
)

target_compile_features(
  zookeeper_service_discovery_integration_tests
  PRIVATE cxx_std_20
)

target_link_libraries(zookeeper_service_discovery_integration_tests PRIVATE
  tinyimx_service_registry
  tinyimx_zookeeper_endpoint_provider
)

add_executable(rpc_multi_target_cache_tests
  tests/rpc/rpc_multi_target_cache_test.cpp
)

target_compile_features(rpc_multi_target_cache_tests PRIVATE cxx_std_20)
target_link_libraries(rpc_multi_target_cache_tests PRIVATE
  tinyimx_rpc_client
)

add_test(
  NAME rpc_multi_target_cache_tests
  COMMAND rpc_multi_target_cache_tests
)

# M15-C: focused rapid-churn and recovery integration gate. External ensemble
# member/leader failures remain shell-driven so this target stays deterministic.
add_executable(zookeeper_failover_recovery_integration_tests
  tests/registry/zookeeper_failover_recovery_integration_test.cpp
)

target_compile_features(
  zookeeper_failover_recovery_integration_tests
  PRIVATE cxx_std_20
)

target_link_libraries(zookeeper_failover_recovery_integration_tests PRIVATE
  tinyimx_service_registry
  tinyimx_zookeeper_endpoint_provider
)

add_executable(zookeeper_discovery_probe
  examples/zookeeper_discovery_probe.cpp
)

target_compile_features(zookeeper_discovery_probe PRIVATE cxx_std_20)
target_link_libraries(zookeeper_discovery_probe PRIVATE
  tinyimx_service_registry
)
