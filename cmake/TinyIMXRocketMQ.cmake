include_guard(GLOBAL)

option(
  TINYIMX_ENABLE_ROCKETMQ_CLIENT
  "Build M16-B RocketMQ transport/relay/projector executables"
  OFF
)

set(
  TINYIMX_ROCKETMQ_PREFIX
  "${CMAKE_CURRENT_SOURCE_DIR}/toolchains/rocketmq-cpp-5.1.1"
  CACHE PATH
  "Installed Apache RocketMQ cpp-5.1.1 prefix"
)

if(NOT TINYIMX_ENABLE_ROCKETMQ_CLIENT)
  message(STATUS "TinyIMX RocketMQ client integration: disabled")
  return()
endif()

foreach(required_target IN ITEMS
    tinyimx_eventing
    tinyimx_outbox
    tinyimx_outbox_relay
    tinyimx_unread_projection
    tinyimx_config
    tinyimx_logging
    tinyimx_db
    tinyimx_cache_service
)
  if(NOT TARGET ${required_target})
    message(FATAL_ERROR
      "TinyIMXRocketMQ.cmake requires target: ${required_target}. "
      "Include this module after TinyIMXMessageService.cmake.")
  endif()
endforeach()

find_path(
  TINYIMX_ROCKETMQ_INCLUDE_DIR
  NAMES rocketmq/Producer.h rocketmq/SimpleConsumer.h
  PATHS "${TINYIMX_ROCKETMQ_PREFIX}/include"
  NO_DEFAULT_PATH
)

find_library(
  TINYIMX_ROCKETMQ_LIBRARY
  NAMES rocketmq
  PATHS
    "${TINYIMX_ROCKETMQ_PREFIX}/lib"
    "${TINYIMX_ROCKETMQ_PREFIX}/lib64"
  NO_DEFAULT_PATH
)

if(NOT TINYIMX_ROCKETMQ_INCLUDE_DIR OR NOT TINYIMX_ROCKETMQ_LIBRARY)
  message(FATAL_ERROR
    "Apache RocketMQ C++ SDK not found under ${TINYIMX_ROCKETMQ_PREFIX}. "
    "Run scripts/bootstrap_rocketmq_cpp_client.sh first.")
endif()

set(_tinyimx_rocketmq_toolchain_meta
  "${TINYIMX_ROCKETMQ_PREFIX}/.tinyimx-rocketmq-isolated-toolchain"
)
if(NOT EXISTS "${_tinyimx_rocketmq_toolchain_meta}")
  message(FATAL_ERROR
    "RocketMQ SDK exists but TinyIMX isolation metadata is missing: "
    "${_tinyimx_rocketmq_toolchain_meta}. Re-run "
    "scripts/bootstrap_rocketmq_cpp_client.sh so cpp-5.1.1 is built against "
    "its isolated gRPC 1.46/protobuf 3.19 dependency family, not TinyIMX's "
    "newer M14 gRPC toolchain.")
endif()

# Do not import rocketmq-config.cmake into the main TinyIMX CMake graph. The
# upstream package exports gRPC/Abseil transitively, while TinyIMX already owns
# a newer gRPC toolchain for M14. M16-B deliberately runs MQ components in
# separate executables; importing only the installed shared library preserves
# that process-level dependency isolation.
add_library(tinyimx_rocketmq_sdk SHARED IMPORTED GLOBAL)
set_target_properties(tinyimx_rocketmq_sdk PROPERTIES
  IMPORTED_LOCATION "${TINYIMX_ROCKETMQ_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${TINYIMX_ROCKETMQ_INCLUDE_DIR}"
)

add_library(tinyimx_rocketmq_transport
  services/eventing/rocketmq/RocketMQProducer.cpp
  services/eventing/rocketmq/RocketMQSimpleConsumer.cpp
)

target_include_directories(tinyimx_rocketmq_transport PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(tinyimx_rocketmq_transport PUBLIC cxx_std_20)
target_link_libraries(tinyimx_rocketmq_transport PUBLIC
  tinyimx_eventing
  tinyimx_logging
  tinyimx_rocketmq_sdk
)

set(_tinyimx_rocketmq_runtime_paths
  "${TINYIMX_ROCKETMQ_PREFIX}/lib"
  "${TINYIMX_ROCKETMQ_PREFIX}/lib64"
)

add_executable(outbox_relay_demo
  examples/outbox_relay_demo.cpp
)
target_compile_features(outbox_relay_demo PRIVATE cxx_std_20)
target_link_libraries(outbox_relay_demo PRIVATE
  tinyimx_config
  tinyimx_logging
  tinyimx_db
  tinyimx_outbox
  tinyimx_outbox_relay
  tinyimx_rocketmq_transport
)
set_target_properties(outbox_relay_demo PROPERTIES
  BUILD_RPATH "${_tinyimx_rocketmq_runtime_paths}"
)

add_executable(unread_projector_demo
  examples/unread_projector_demo.cpp
)
target_compile_features(unread_projector_demo PRIVATE cxx_std_20)
target_link_libraries(unread_projector_demo PRIVATE
  tinyimx_config
  tinyimx_logging
  tinyimx_db
  tinyimx_cache
  tinyimx_cache_service
  tinyimx_unread_projection
  tinyimx_rocketmq_transport
)
set_target_properties(unread_projector_demo PROPERTIES
  BUILD_RPATH "${_tinyimx_rocketmq_runtime_paths}"
)

add_executable(rocketmq_transport_integration_tests
  tests/eventing/rocketmq_transport_integration_test.cpp
)
target_compile_features(rocketmq_transport_integration_tests PRIVATE cxx_std_20)
target_link_libraries(rocketmq_transport_integration_tests PRIVATE
  tinyimx_config
  tinyimx_logging
  tinyimx_rocketmq_transport
)
set_target_properties(rocketmq_transport_integration_tests PROPERTIES
  BUILD_RPATH "${_tinyimx_rocketmq_runtime_paths}"
)


# ---------------------------------------------------------------------------
# M16-C real runtime fault/recovery acceptance.
# Uses real MySQL / Redis / RocketMQ with deterministic crash decorators.
# ---------------------------------------------------------------------------
add_executable(m16_fault_recovery_runtime_test
  tests/reliability/m16_fault_recovery_runtime_test.cpp
)

target_compile_features(
  m16_fault_recovery_runtime_test
  PRIVATE cxx_std_20
)

target_link_libraries(
  m16_fault_recovery_runtime_test
  PRIVATE
    tinyimx_config
    tinyimx_logging
    tinyimx_db
    tinyimx_cache
    tinyimx_cache_service
    tinyimx_outbox
    tinyimx_outbox_relay
    tinyimx_unread_projection
    tinyimx_rocketmq_transport
)

set_target_properties(
  m16_fault_recovery_runtime_test
  PROPERTIES
    BUILD_RPATH "${_tinyimx_rocketmq_runtime_paths}"
)
