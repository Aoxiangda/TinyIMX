# Test-only target; never installed or substituted for gateway_demo.
get_target_property(_p1b_demo_libs gateway_demo LINK_LIBRARIES)
add_executable(tinyimx_p1b_replay_fixture EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_LIST_DIR}/fixture_gateway.cpp")
target_link_libraries(tinyimx_p1b_replay_fixture PRIVATE ${_p1b_demo_libs})
target_include_directories(tinyimx_p1b_replay_fixture PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_LIST_DIR}")
target_compile_features(tinyimx_p1b_replay_fixture PRIVATE cxx_std_20)
foreach(_p1b_prop IN ITEMS COMPILE_DEFINITIONS COMPILE_OPTIONS INCLUDE_DIRECTORIES LINK_OPTIONS)
  get_target_property(_p1b_value gateway_demo ${_p1b_prop})
  if(_p1b_value)
    set_property(TARGET tinyimx_p1b_replay_fixture APPEND PROPERTY ${_p1b_prop} "${_p1b_value}")
  endif()
endforeach()
unset(_p1b_demo_libs)
unset(_p1b_prop)
unset(_p1b_value)
