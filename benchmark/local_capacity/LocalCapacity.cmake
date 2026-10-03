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
