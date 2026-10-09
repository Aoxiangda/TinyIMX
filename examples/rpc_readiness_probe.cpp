#include "common/runtime/RpcReadinessProbe.h"
#include <iostream>
int main(int argc, char* argv[]) {
    if (argc != 2) { std::cerr << "usage: rpc_readiness_probe HOST:PORT\n"; return 2; }
    const bool serving = tinyimx::runtime::CheckRpcReadiness(argv[1]);
    std::cout << (serving ? "SERVING\n" : "NOT_SERVING\n");
    return serving ? 0 : 1;
}
