include_guard(GLOBAL)

foreach(required_target IN ITEMS
    tinyimx_config
    tinyimx_logging
    tinyimx_concurrency
    tinyimx_net
    tinyimx_rpc_client
    tinyimx_service_registry
    tinyimx_zookeeper_endpoint_provider
)
  if(NOT TARGET ${required_target})
    message(FATAL_ERROR
      "TinyIMXMcp.cmake requires target: ${required_target}. "
      "Include this module near the end of root CMakeLists.txt.")
  endif()
endforeach()

add_library(tinyimx_mcp_core
  services/intelligence/mcp/McpTypes.cpp
  services/intelligence/mcp/McpRegistry.cpp
  services/intelligence/mcp/McpAuth.cpp
  services/intelligence/mcp/McpDispatcher.cpp
  services/intelligence/mcp/HttpCodec.cpp
)

target_include_directories(tinyimx_mcp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tinyimx_mcp_core PUBLIC cxx_std_20)
target_link_libraries(tinyimx_mcp_core PUBLIC
  nlohmann_json::nlohmann_json
  tinyimx_net
)

add_library(tinyimx_mcp_domain
  services/intelligence/mcp/McpDomainTools.cpp
)

target_include_directories(tinyimx_mcp_domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tinyimx_mcp_domain PUBLIC cxx_std_20)
target_link_libraries(tinyimx_mcp_domain PUBLIC
  tinyimx_mcp_core
)

add_library(tinyimx_mcp_domain_rpc
  services/intelligence/mcp/McpRpcDomainBackend.cpp
)

target_include_directories(tinyimx_mcp_domain_rpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tinyimx_mcp_domain_rpc PUBLIC cxx_std_20)
target_link_libraries(tinyimx_mcp_domain_rpc PUBLIC
  tinyimx_mcp_domain
  tinyimx_rpc_client
)

add_library(tinyimx_mcp_runtime
  services/intelligence/mcp/McpServer.cpp
)

target_include_directories(tinyimx_mcp_runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tinyimx_mcp_runtime PUBLIC cxx_std_20)
target_link_libraries(tinyimx_mcp_runtime PUBLIC
  tinyimx_mcp_core
  tinyimx_concurrency
  tinyimx_logging
  tinyimx_config
  tinyimx_net
)

add_executable(m19_mcp_core_tests tests/mcp/mcp_core_test.cpp)
target_compile_features(m19_mcp_core_tests PRIVATE cxx_std_20)
target_link_libraries(m19_mcp_core_tests PRIVATE tinyimx_mcp_core)
add_test(NAME tinyimx.m19.mcp_core COMMAND m19_mcp_core_tests)
set_tests_properties(tinyimx.m19.mcp_core PROPERTIES LABELS "m19;mcp;unit")

add_executable(m19_mcp_domain_tools_tests tests/mcp/mcp_domain_tools_test.cpp)
target_compile_features(m19_mcp_domain_tools_tests PRIVATE cxx_std_20)
target_link_libraries(m19_mcp_domain_tools_tests PRIVATE
  tinyimx_mcp_domain
  tinyimx_mcp_core
)
add_test(NAME tinyimx.m19.mcp_domain_tools COMMAND m19_mcp_domain_tools_tests)
set_tests_properties(tinyimx.m19.mcp_domain_tools PROPERTIES LABELS "m19;mcp;unit;domain")

add_executable(tinyimx_mcp_server examples/mcp_server_demo.cpp)

add_executable(tinyimx_desktop_file_server examples/desktop_file_server.cpp)
target_compile_features(tinyimx_desktop_file_server PRIVATE cxx_std_20)
target_link_libraries(tinyimx_desktop_file_server PRIVATE tinyimx_mcp_core
  tinyimx_rpc_client tinyimx_concurrency tinyimx_logging tinyimx_config
  OpenSSL::Crypto)
target_compile_features(tinyimx_mcp_server PRIVATE cxx_std_20)
target_link_libraries(tinyimx_mcp_server PRIVATE
  tinyimx_mcp_runtime
  tinyimx_mcp_domain
  tinyimx_mcp_domain_rpc
  tinyimx_rpc_client
  tinyimx_service_registry
  tinyimx_zookeeper_endpoint_provider
  tinyimx_logging
  tinyimx_config
)
