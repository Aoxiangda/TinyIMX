include_guard(GLOBAL)

foreach(required_target IN ITEMS
    tinyimx_config
    tinyimx_logging
    tinyimx_concurrency
    tinyimx_net
)
  if(NOT TARGET ${required_target})
    message(FATAL_ERROR
      "TinyIMXObservability.cmake requires target: ${required_target}. "
      "Include this module near the end of root CMakeLists.txt.")
  endif()
endforeach()

find_package(opentelemetry-cpp CONFIG REQUIRED)

foreach(required_otel_target IN ITEMS
    opentelemetry-cpp::otlp_grpc_metrics_exporter
    opentelemetry-cpp::otlp_grpc_exporter
)
  if(NOT TARGET ${required_otel_target})
    message(FATAL_ERROR
      "M20 observability requires OpenTelemetry target: ${required_otel_target}")
  endif()
endforeach()

add_library(tinyimx_observability
  common/observability/Metrics.cpp
  common/observability/TelemetryRuntime.cpp
  common/observability/ProcessTelemetry.cpp
  common/observability/Trace.cpp
  common/observability/GrpcTracing.cpp
)

target_include_directories(tinyimx_observability PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(tinyimx_observability PUBLIC cxx_std_20)

target_link_libraries(tinyimx_observability PUBLIC
  tinyimx_config
  opentelemetry-cpp::otlp_grpc_metrics_exporter
  opentelemetry-cpp::otlp_grpc_exporter
  gRPC::grpc++
)

# Instrumentation is implemented in retained core .cpp files; OpenTelemetry
# remains private to the implementation boundary.
target_link_libraries(tinyimx_concurrency PRIVATE tinyimx_observability)
target_link_libraries(tinyimx_net PRIVATE tinyimx_observability)

add_executable(m20_observability_config_tests
  tests/observability/observability_config_test.cpp
)
target_compile_features(m20_observability_config_tests PRIVATE cxx_std_20)
target_link_libraries(m20_observability_config_tests PRIVATE tinyimx_config)
add_test(NAME tinyimx.m20.observability_config COMMAND m20_observability_config_tests)
set_tests_properties(tinyimx.m20.observability_config PROPERTIES LABELS "m20;observability;unit")

add_executable(m20_observability_runtime_tests
  tests/observability/observability_runtime_test.cpp
)
target_compile_features(m20_observability_runtime_tests PRIVATE cxx_std_20)
target_link_libraries(m20_observability_runtime_tests PRIVATE
  tinyimx_observability
  tinyimx_concurrency
)
add_test(NAME tinyimx.m20.observability_runtime COMMAND m20_observability_runtime_tests)
set_tests_properties(tinyimx.m20.observability_runtime PROPERTIES LABELS "m20;observability;unit")

# M20 tracing integration remains private to implementation targets.
foreach(observed_target IN ITEMS
    tinyimx_logging
    tinyimx_rpc_client
    tinyimx_user_grpc
    tinyimx_social_grpc
    tinyimx_message_grpc
    tinyimx_group_grpc
    tinyimx_file_grpc
    tinyimx_mcp_core
    tinyimx_mcp_runtime
    tinyimx_mcp_client
    tinyimx_ai_provider
    tinyimx_ai_agent
    user_service_demo
    social_service_demo
    message_service_demo
    group_service_demo
    file_service_demo
    tinyimx_mcp_server
    tinyimx_ai_agent_demo
)
  if(TARGET ${observed_target})
    target_link_libraries(${observed_target} PRIVATE tinyimx_observability)
  endif()
endforeach()

add_executable(m20_ai_observability_config_tests
  tests/observability/ai_observability_config_test.cpp
)
target_compile_features(m20_ai_observability_config_tests PRIVATE cxx_std_20)
target_link_libraries(m20_ai_observability_config_tests PRIVATE tinyimx_ai_agent)
add_test(NAME tinyimx.m20.ai_observability_config COMMAND m20_ai_observability_config_tests)
set_tests_properties(tinyimx.m20.ai_observability_config PROPERTIES
  LABELS "m20;observability;tracing;ai;unit")

add_executable(m20_trace_context_tests
  tests/observability/trace_context_test.cpp
)
target_compile_features(m20_trace_context_tests PRIVATE cxx_std_20)
target_link_libraries(m20_trace_context_tests PRIVATE
  tinyimx_observability
  tinyimx_logging
)
add_test(NAME tinyimx.m20.trace_context COMMAND m20_trace_context_tests)
set_tests_properties(tinyimx.m20.trace_context PROPERTIES
  LABELS "m20;observability;tracing;unit")

add_executable(tinyimx_observability_demo
  examples/observability_demo.cpp
)
target_compile_features(tinyimx_observability_demo PRIVATE cxx_std_20)
target_link_libraries(tinyimx_observability_demo PRIVATE
  tinyimx_config
  tinyimx_logging
  tinyimx_observability
  tinyimx_concurrency
  tinyimx_net
)

# M20: deterministic Agent -> MCP -> gRPC real multi-process trace E2E driver.
add_executable(m20_trace_agent_e2e
  tests/observability/trace_agent_e2e.cpp
)
target_compile_features(m20_trace_agent_e2e PRIVATE cxx_std_20)
target_link_libraries(m20_trace_agent_e2e PRIVATE
  tinyimx_ai_agent
  tinyimx_mcp_client
  tinyimx_ai_http
  tinyimx_observability
  tinyimx_logging
)
