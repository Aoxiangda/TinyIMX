# ============================================================
# TinyIMX Third-Party Dependencies
#
# C/C++ third-party client/runtime libraries are resolved
# through the vcpkg Manifest toolchain.
#
# MySQL Server / Redis Server are runtime infrastructure and
# are intentionally not managed here.
# ============================================================

find_package(
    nlohmann_json
    CONFIG
    REQUIRED
)

find_package(
    OpenSSL
    REQUIRED
)

find_package(
    hiredis
    CONFIG
    REQUIRED
)

find_package(
    unofficial-libmysql
    CONFIG
    REQUIRED
)

find_package(
    Protobuf
    CONFIG
    REQUIRED
)

find_package(
    gRPC
    CONFIG
    REQUIRED
)
# M15-A: ZooKeeper C multithreaded/synchronous client.
find_package(
    unofficial-zookeeper
    CONFIG
    REQUIRED
)
