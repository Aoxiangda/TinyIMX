#pragma once
#include "services/projection/unread/UnreadProjectionReader.h"
#include "tinyimx/message/v1/message_service.grpc.pb.h"
#include <chrono>
#include <memory>
namespace tinyimx::projection::unread {
class UnreadSnapshotRpcService final : public message::v1::UnreadSnapshotService::Service {
public:
    explicit UnreadSnapshotRpcService(UnreadProjectionSource* source);
    grpc::Status ReadDialogUnreadSnapshot(grpc::ServerContext*,const message::v1::ReadDialogUnreadSnapshotRequest*,message::v1::ReadDialogUnreadSnapshotResponse*)override;
    grpc::Status ReadUserUnreadSnapshot(grpc::ServerContext*,const message::v1::ReadUserUnreadSnapshotRequest*,message::v1::ReadUserUnreadSnapshotResponse*)override;
private:
    UnreadProjectionSource* source_;
};
class UnreadSnapshotRpcClient final : public UnreadProjectionSource {
public:
    explicit UnreadSnapshotRpcClient(const std::string& target,std::chrono::milliseconds timeout=std::chrono::milliseconds(1000));
    UnreadProjectionReadResult<DialogUnreadSnapshot> LoadDialogSnapshot(std::uint64_t receiver,std::uint64_t peer)override;
    UnreadProjectionReadResult<UserUnreadSnapshot> LoadUserSnapshot(std::uint64_t receiver)override;
private:
    std::unique_ptr<message::v1::UnreadSnapshotService::Stub> stub_;
    std::chrono::milliseconds timeout_;
};
}
