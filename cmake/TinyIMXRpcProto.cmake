include_guard(GLOBAL)

if(NOT TARGET protobuf::libprotobuf)
  message(FATAL_ERROR
    "TinyIMXRpcProto.cmake requires protobuf::libprotobuf. "
    "Include TinyIMXDependencies.cmake first.")
endif()

if(NOT TARGET protobuf::protoc)
  message(FATAL_ERROR
    "TinyIMXRpcProto.cmake requires protobuf::protoc from the pinned vcpkg toolchain.")
endif()

if(NOT TARGET gRPC::grpc++)
  message(FATAL_ERROR
    "TinyIMXRpcProto.cmake requires gRPC::grpc++. "
    "Include TinyIMXDependencies.cmake first.")
endif()

if(NOT TARGET gRPC::grpc_cpp_plugin)
  message(FATAL_ERROR
    "TinyIMXRpcProto.cmake requires gRPC::grpc_cpp_plugin (grpc[codegen]).")
endif()

set(TINYIMX_PROTO_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(TINYIMX_RPC_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated/rpc")

set(TINYIMX_RPC_PROTO_FILES
    "${TINYIMX_PROTO_ROOT}/tinyimx/common/v1/common.proto"
    "${TINYIMX_PROTO_ROOT}/tinyimx/social/v1/social_service.proto"
    "${TINYIMX_PROTO_ROOT}/tinyimx/user/v1/user_service.proto"
    "${TINYIMX_PROTO_ROOT}/tinyimx/message/v1/message_service.proto"
    "${TINYIMX_PROTO_ROOT}/tinyimx/group/v1/group_service.proto"
    "${TINYIMX_PROTO_ROOT}/tinyimx/file/v1/file_service.proto")

set(TINYIMX_RPC_GRPC_PROTO_FILES
    "${TINYIMX_PROTO_ROOT}/tinyimx/social/v1/social_service.proto"
    "${TINYIMX_PROTO_ROOT}/tinyimx/user/v1/user_service.proto"
    "${TINYIMX_PROTO_ROOT}/tinyimx/message/v1/message_service.proto"
    "${TINYIMX_PROTO_ROOT}/tinyimx/group/v1/group_service.proto"
    "${TINYIMX_PROTO_ROOT}/tinyimx/file/v1/file_service.proto")

foreach(proto_file IN LISTS TINYIMX_RPC_PROTO_FILES)
  if(NOT EXISTS "${proto_file}")
    message(FATAL_ERROR "TinyIMX RPC proto not found: ${proto_file}")
  endif()
endforeach()

file(MAKE_DIRECTORY "${TINYIMX_RPC_GENERATED_DIR}")

# Generate normal protobuf C++ messages for all RPC schemas.
protobuf_generate(
  OUT_VAR TINYIMX_RPC_PROTO_GENERATED
  LANGUAGE cpp
  PROTOS ${TINYIMX_RPC_PROTO_FILES}
  IMPORT_DIRS "${TINYIMX_PROTO_ROOT}"
  PROTOC_OUT_DIR "${TINYIMX_RPC_GENERATED_DIR}")

# Generate gRPC C++ service/stub code only for schemas that declare services.
protobuf_generate(
  OUT_VAR TINYIMX_RPC_GRPC_GENERATED
  LANGUAGE grpc
  GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
  PLUGIN "protoc-gen-grpc=\$<TARGET_FILE:gRPC::grpc_cpp_plugin>"
  PROTOS ${TINYIMX_RPC_GRPC_PROTO_FILES}
  IMPORT_DIRS "${TINYIMX_PROTO_ROOT}"
  PROTOC_OUT_DIR "${TINYIMX_RPC_GENERATED_DIR}")

set(TINYIMX_RPC_GENERATED_FILES
    ${TINYIMX_RPC_PROTO_GENERATED}
    ${TINYIMX_RPC_GRPC_GENERATED})

# Ensure every generated header/source exists before compilation starts. This
# avoids first-build ordering races between *.grpc.pb.cc and *.pb.h.
add_custom_target(tinyimx_rpc_codegen
  DEPENDS ${TINYIMX_RPC_GENERATED_FILES})

add_library(tinyimx_rpc_proto STATIC
  ${TINYIMX_RPC_GENERATED_FILES})

add_dependencies(tinyimx_rpc_proto tinyimx_rpc_codegen)

target_compile_features(tinyimx_rpc_proto PUBLIC cxx_std_20)

target_include_directories(tinyimx_rpc_proto
  PUBLIC
    "$<BUILD_INTERFACE:${TINYIMX_RPC_GENERATED_DIR}>")

target_link_libraries(tinyimx_rpc_proto
  PUBLIC
    protobuf::libprotobuf
    gRPC::grpc++)

# Protobuf-generated sources are not suitable for unity/jumbo compilation.
set_source_files_properties(
  ${TINYIMX_RPC_GENERATED_FILES}
  PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
