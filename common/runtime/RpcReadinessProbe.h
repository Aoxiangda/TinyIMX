#pragma once
#include "common/runtime/RpcReadiness.h"
#include <grpcpp/grpcpp.h>
#include <grpcpp/generic/generic_stub.h>
#include <chrono>
#include <string>
#include <vector>
namespace tinyimx::runtime {
inline bool CheckRpcReadiness(const std::string& target) {
    if (target.empty()) return false;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(2));
    grpc::GenericStub stub(grpc::CreateChannel(target, grpc::InsecureChannelCredentials()));
    grpc::CompletionQueue queue;
    // grpc.health.v1 CheckRequest: field 1 is a length-delimited service name.
    // Fixed ASCII name only; no user input is serialized.
    constexpr std::size_t length = sizeof(kRpcReadinessService) - 1;
    static_assert(length < 128);
    std::string request("\x0a", 1);
    request.push_back(static_cast<char>(length));
    request.append(kRpcReadinessService, length);
    grpc::Slice slice(request);
    grpc::ByteBuffer input(&slice, 1), output;
    grpc::Status status;
    auto call = stub.PrepareUnaryCall(&context, "/grpc.health.v1.Health/Check", input, &queue);
    if (call == nullptr) { queue.Shutdown(); return false; }
    call->StartCall();
    int tag_value = 0;
    call->Finish(&output, &status, &tag_value);
    void* tag = nullptr;
    bool ok = false;
    const bool completed = queue.Next(&tag, &ok);
    queue.Shutdown();
    void* drained_tag = nullptr;
    bool drained_ok = false;
    while (queue.Next(&drained_tag, &drained_ok)) {}
    if (!completed || !ok || tag != &tag_value || !status.ok()) return false;
    std::vector<grpc::Slice> slices;
    if (!output.Dump(&slices).ok()) return false;
    std::string response;
    for (const auto& part : slices) {
        if (response.size() + part.size() > 16) return false;
        response.append(reinterpret_cast<const char*>(part.begin()), part.size());
    }
    // CheckResponse status=SERVING(1). Reject unknown, malformed and extra fields.
    return response == std::string("\x08\x01", 2);
}
}  // namespace tinyimx::runtime
