#include "services/rpc/FileRpcClient.h"
#include "services/rpc/StaticServiceEndpointProvider.h"
#include "tinyimx/file/v1/file_service.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>

namespace {

bool Expect(bool condition, const char* name) {
    if (condition) { std::cout << "[PASS] " << name << '\n'; return true; }
    std::cerr << "[FAIL] " << name << '\n'; return false;
}

tinyimx::rpc::RpcCallOptions Options(std::chrono::milliseconds timeout) {
    tinyimx::rpc::RpcCallOptions o;
    o.request_id = "m18-a2-file-client";
    o.trace_id = "m18-a2-file-client";
    o.caller_service = "test";
    o.caller_instance = "test-1";
    o.remaining_timeout = timeout;
    return o;
}

class FakeFileService final : public tinyimx::file::v1::FileService::Service {
public:
    grpc::Status BeginUpload(grpc::ServerContext*,
                             const tinyimx::file::v1::BeginUploadRequest* request,
                             tinyimx::file::v1::BeginUploadResponse* response) override {
        auto* f = response->mutable_file();
        f->set_file_id(7001); f->set_owner_user_id(request->actor_user_id());
        f->set_file_name(request->file_name()); f->set_content_type(request->content_type());
        f->set_total_size(request->total_size()); f->set_checksum_algorithm("sha256");
        f->set_expected_checksum(request->expected_checksum()); f->set_storage_backend("local_fs");
        f->set_storage_key("files/7001"); f->set_status(tinyimx::file::v1::FILE_STATUS_UPLOADING); f->set_version(1);
        auto* s = response->mutable_session();
        s->set_upload_id(77); s->set_file_id(7001); s->set_owner_user_id(request->actor_user_id());
        s->set_client_upload_id(request->client_upload_id()); s->set_total_size(request->total_size());
        s->set_chunk_size(4ULL*1024ULL*1024ULL); s->set_status(tinyimx::file::v1::UPLOAD_SESSION_STATUS_ACTIVE); s->set_version(1);
        response->set_result(tinyimx::file::v1::BEGIN_UPLOAD_RESULT_CREATED);
        response->set_message("created");
        return grpc::Status::OK;
    }
    grpc::Status GetUploadSession(grpc::ServerContext*,
                                  const tinyimx::file::v1::GetUploadSessionRequest* request,
                                  tinyimx::file::v1::GetUploadSessionResponse* response) override {
        if (request->upload_id() == 404) return grpc::Status(grpc::StatusCode::NOT_FOUND, "missing");
        auto* f=response->mutable_file(); f->set_file_id(7001); f->set_owner_user_id(request->actor_user_id()); f->set_file_name("a.bin"); f->set_content_type("application/octet-stream"); f->set_total_size(1024); f->set_checksum_algorithm("sha256"); f->set_expected_checksum(std::string(64,'a')); f->set_storage_backend("local_fs"); f->set_storage_key("files/7001"); f->set_status(tinyimx::file::v1::FILE_STATUS_UPLOADING); f->set_version(1);
        auto* s=response->mutable_session(); s->set_upload_id(request->upload_id()); s->set_file_id(7001); s->set_owner_user_id(request->actor_user_id()); s->set_client_upload_id("id"); s->set_total_size(1024); s->set_chunk_size(262144); s->set_status(tinyimx::file::v1::UPLOAD_SESSION_STATUS_ACTIVE); s->set_version(1);
        return grpc::Status::OK;
    }
    grpc::Status CancelUpload(grpc::ServerContext*,
                              const tinyimx::file::v1::CancelUploadRequest* request,
                              tinyimx::file::v1::CancelUploadResponse* response) override {
        auto* f=response->mutable_file(); f->set_file_id(7001); f->set_owner_user_id(request->actor_user_id()); f->set_file_name("a.bin"); f->set_content_type("application/octet-stream"); f->set_total_size(1024); f->set_checksum_algorithm("sha256"); f->set_expected_checksum(std::string(64,'a')); f->set_storage_backend("local_fs"); f->set_storage_key("files/7001"); f->set_status(tinyimx::file::v1::FILE_STATUS_CANCELED); f->set_version(2);
        auto* s=response->mutable_session(); s->set_upload_id(request->upload_id()); s->set_file_id(7001); s->set_owner_user_id(request->actor_user_id()); s->set_client_upload_id("id"); s->set_total_size(1024); s->set_chunk_size(262144); s->set_status(tinyimx::file::v1::UPLOAD_SESSION_STATUS_CANCELED); s->set_version(2);
        response->set_result(tinyimx::file::v1::CANCEL_UPLOAD_RESULT_APPLIED); response->set_message("canceled");
        return grpc::Status::OK;
    }
};

}  // namespace

int main() {
    using namespace tinyimx::rpc;
    bool ok = true;
    FakeFileService service;
    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    ok &= Expect(server != nullptr && port > 0, "FileRpcClient fake FileService started");
    if (!server) return 1;

    auto provider = std::make_shared<StaticServiceEndpointProvider>("", "", "", "", "127.0.0.1:" + std::to_string(port));
    const auto endpoint = provider->Resolve(ServiceKind::kFile);
    ok &= Expect(endpoint && endpoint->target == "127.0.0.1:" + std::to_string(port),
                 "StaticServiceEndpointProvider resolves File target");
    FileRpcClient client(provider);

    BeginUploadRpcRequest begin;
    begin.actor_user_id=10001; begin.client_upload_id="u1"; begin.file_name="a.bin"; begin.content_type="application/octet-stream"; begin.total_size=1024; begin.checksum_algorithm="sha256"; begin.expected_checksum=std::string(64,'a');
    auto created=client.BeginUpload(begin,Options(std::chrono::milliseconds(1000)));
    ok &= Expect(created.ok() && created.value->bundle.file.owner_user_id==10001 && created.value->bundle.session.upload_id==77,
                 "FileRpcClient BeginUpload real gRPC mapping");

    auto invalid=begin; invalid.actor_user_id=0;
    const auto invalid_result=client.BeginUpload(invalid,Options(std::chrono::milliseconds(10)));
    ok &= Expect(!invalid_result.ok() && invalid_result.status.code==RpcErrorCode::kInvalidArgument,
                 "FileRpcClient rejects invalid actor before network");

    const auto deadline=client.GetUploadSession({10001,77},Options(std::chrono::milliseconds(0)));
    ok &= Expect(!deadline.ok() && deadline.status.code==RpcErrorCode::kDeadlineExceeded,
                 "FileRpcClient fails closed on exhausted budget");

    const auto missing=client.GetUploadSession({10001,404},Options(std::chrono::milliseconds(1000)));
    ok &= Expect(!missing.ok() && missing.status.code==RpcErrorCode::kNotFound,
                 "FileRpcClient maps NOT_FOUND");

    const auto canceled=client.CancelUpload({10001,77},Options(std::chrono::milliseconds(1000)));
    ok &= Expect(canceled.ok() && canceled.value->bundle.file.owner_user_id==10001,
                 "FileRpcClient CancelUpload mapping");

    auto no_file_provider=std::make_shared<StaticServiceEndpointProvider>("","","");
    FileRpcClient no_file(no_file_provider);
    const auto unavailable=no_file.GetUploadSession({10001,77},Options(std::chrono::milliseconds(10)));
    ok &= Expect(!unavailable.ok() && unavailable.status.code==RpcErrorCode::kUnavailable,
                 "FileRpcClient fails closed when File endpoint missing");

    server->Shutdown();
    std::cout << (ok ? "[PASS] M18-A2 FileRpcClient contract tests\n" : "[FAIL] M18-A2 FileRpcClient contract tests\n");
    return ok ? 0 : 1;
}
