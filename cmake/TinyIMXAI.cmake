include_guard(GLOBAL)

foreach(required_target IN ITEMS
    tinyimx_net
    tinyimx_logging
    tinyimx_mcp_core
)
  if(NOT TARGET ${required_target})
    message(FATAL_ERROR
      "TinyIMXAI.cmake requires target: ${required_target}. "
      "Include TinyIMXAI.cmake after TinyIMXMcp.cmake.")
  endif()
endforeach()

add_library(tinyimx_ai_http
  services/intelligence/http/HttpClient.cpp
)
target_include_directories(tinyimx_ai_http PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tinyimx_ai_http PUBLIC cxx_std_20)
target_link_libraries(tinyimx_ai_http PUBLIC tinyimx_net)

add_library(tinyimx_mcp_client
  services/intelligence/mcp/McpClient.cpp
)
target_include_directories(tinyimx_mcp_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tinyimx_mcp_client PUBLIC cxx_std_20)
target_link_libraries(tinyimx_mcp_client PUBLIC
  tinyimx_ai_http
  tinyimx_mcp_core
  nlohmann_json::nlohmann_json
)

add_library(tinyimx_ai_provider
  services/intelligence/ai/OpenAICompatibleProvider.cpp
)
target_include_directories(tinyimx_ai_provider PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tinyimx_ai_provider PUBLIC cxx_std_20)
target_link_libraries(tinyimx_ai_provider PUBLIC
  tinyimx_ai_http
  nlohmann_json::nlohmann_json
)

add_library(tinyimx_ai_agent
  services/intelligence/ai/AgentOrchestrator.cpp
  services/intelligence/ai/AIRuntimeConfig.cpp
)
target_include_directories(tinyimx_ai_agent PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(tinyimx_ai_agent PUBLIC cxx_std_20)
target_link_libraries(tinyimx_ai_agent PUBLIC
  tinyimx_ai_provider
  tinyimx_mcp_client
  nlohmann_json::nlohmann_json
)

add_executable(m19_http_client_tests tests/ai/http_client_test.cpp)
target_compile_features(m19_http_client_tests PRIVATE cxx_std_20)
target_link_libraries(m19_http_client_tests PRIVATE tinyimx_ai_http)
add_test(NAME tinyimx.m19.http_client COMMAND m19_http_client_tests)
set_tests_properties(tinyimx.m19.http_client PROPERTIES LABELS "m19;ai;http;unit")

add_executable(m19_mcp_client_tests tests/ai/mcp_client_test.cpp)
target_compile_features(m19_mcp_client_tests PRIVATE cxx_std_20)
target_link_libraries(m19_mcp_client_tests PRIVATE tinyimx_mcp_client)
add_test(NAME tinyimx.m19.mcp_client COMMAND m19_mcp_client_tests)
set_tests_properties(tinyimx.m19.mcp_client PROPERTIES LABELS "m19;ai;mcp;unit")

add_executable(m19_ai_provider_tests tests/ai/ai_provider_test.cpp)
target_compile_features(m19_ai_provider_tests PRIVATE cxx_std_20)
target_link_libraries(m19_ai_provider_tests PRIVATE tinyimx_ai_provider)
add_test(NAME tinyimx.m19.ai_provider COMMAND m19_ai_provider_tests)
set_tests_properties(tinyimx.m19.ai_provider PROPERTIES LABELS "m19;ai;provider;unit")

add_executable(m19_agent_orchestrator_tests tests/ai/agent_orchestrator_test.cpp)
target_compile_features(m19_agent_orchestrator_tests PRIVATE cxx_std_20)
target_link_libraries(m19_agent_orchestrator_tests PRIVATE tinyimx_ai_agent)
add_test(NAME tinyimx.m19.agent_orchestrator COMMAND m19_agent_orchestrator_tests)
set_tests_properties(tinyimx.m19.agent_orchestrator PROPERTIES LABELS "m19;ai;agent;unit")

add_executable(tinyimx_ai_agent_demo examples/ai_agent_demo.cpp)
target_compile_features(tinyimx_ai_agent_demo PRIVATE cxx_std_20)
target_link_libraries(tinyimx_ai_agent_demo PRIVATE
  tinyimx_ai_agent
  tinyimx_logging
)
