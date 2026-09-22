#include "services/file/repository/FileRepositoryAdapter.h"

#include "common/db/MySqlConnection.h"
#include "common/db/MySqlConnectionPool.h"
#include "services/repository/FileRepository.h"

#include <openssl/evp.h>

#include <algorithm>
#include <climits>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace tinyimx::file {
namespace {

FileApplicationStatus MapStorageStatus(tinyimx::FileRepositoryStatus status) {
    switch (status) {
        case tinyimx::FileRepositoryStatus::kSucceeded:
            return FileApplicationStatus::kSucceeded;
        case tinyimx::FileRepositoryStatus::kInvalidArgument:
            return FileApplicationStatus::kInvalidArgument;
        case tinyimx::FileRepositoryStatus::kNotFound:
            return FileApplicationStatus::kNotFound;
        case tinyimx::FileRepositoryStatus::kInvalidRecord:
            return FileApplicationStatus::kInvalidRecord;
        case tinyimx::FileRepositoryStatus::kStorageError:
            return FileApplicationStatus::kStorageError;
    }
    return FileApplicationStatus::kStorageError;
}

std::optional<FileStatus> MapFileStatus(std::uint32_t status) {
    switch (status) {
        case 1: return FileStatus::kUploading;
        case 2: return FileStatus::kVerifying;
        case 3: return FileStatus::kAvailable;
        case 4: return FileStatus::kFailed;
        case 5: return FileStatus::kCanceled;
        case 6: return FileStatus::kExpired;
        case 7: return FileStatus::kDeleted;
        default: return std::nullopt;
    }
}

std::optional<UploadSessionStatus> MapUploadStatus(std::uint32_t status) {
    switch (status) {
        case 1: return UploadSessionStatus::kActive;
        case 2: return UploadSessionStatus::kFinalizing;
        case 3: return UploadSessionStatus::kCompleted;
        case 4: return UploadSessionStatus::kCanceled;
        case 5: return UploadSessionStatus::kExpired;
        default: return std::nullopt;
    }
}

std::optional<UploadChunkStatus> MapChunkStatus(std::uint32_t status) {
    switch (status) {
        case 1: return UploadChunkStatus::kReserved;
        case 2: return UploadChunkStatus::kStored;
        default: return std::nullopt;
    }
}

std::optional<UploadChunkView> ToChunkView(const tinyimx::FileUploadChunkRecord& record) {
    const auto status = MapChunkStatus(record.status);
    if (!status.has_value() || record.upload_id == 0 || record.file_id == 0 ||
        record.owner_user_id == 0 || record.chunk_size == 0 ||
        record.checksum_algorithm != "sha256" || record.checksum.size() != 64 ||
        record.storage_part_key.empty()) {
        return std::nullopt;
    }
    UploadChunkView view;
    view.upload_id = record.upload_id;
    view.chunk_index = record.chunk_index;
    view.file_id = record.file_id;
    view.owner_user_id = record.owner_user_id;
    view.byte_offset = record.byte_offset;
    view.chunk_size = record.chunk_size;
    view.checksum_algorithm = record.checksum_algorithm;
    view.checksum = record.checksum;
    view.storage_part_key = record.storage_part_key;
    view.status = *status;
    view.version = record.version;
    view.created_at = record.created_at;
    view.updated_at = record.updated_at;
    view.stored_at = record.stored_at;
    return view;
}

std::optional<UploadBundleView> ToView(const tinyimx::FileUploadBundleRecord& record) {
    const auto file_status = MapFileStatus(record.file.status);
    const auto upload_status = MapUploadStatus(record.session.status);
    if (!file_status.has_value() || !upload_status.has_value() ||
        record.file.file_id == 0 || record.session.upload_id == 0 ||
        record.file.file_id != record.session.file_id ||
        record.file.owner_user_id != record.session.owner_user_id ||
        record.file.total_size != record.session.total_size) {
        return std::nullopt;
    }

    UploadBundleView view;
    view.file.file_id = record.file.file_id;
    view.file.owner_user_id = record.file.owner_user_id;
    view.file.file_name = record.file.file_name;
    view.file.content_type = record.file.content_type;
    view.file.total_size = record.file.total_size;
    view.file.checksum_algorithm = record.file.checksum_algorithm;
    view.file.expected_checksum = record.file.expected_checksum;
    view.file.verified_checksum = record.file.verified_checksum;
    view.file.storage_backend = record.file.storage_backend;
    view.file.storage_key = record.file.storage_key;
    view.file.status = *file_status;
    view.file.version = record.file.version;
    view.file.created_at = record.file.created_at;
    view.file.updated_at = record.file.updated_at;
    view.file.available_at = record.file.available_at;
    view.file.expires_at = record.file.expires_at;

    view.session.upload_id = record.session.upload_id;
    view.session.file_id = record.session.file_id;
    view.session.owner_user_id = record.session.owner_user_id;
    view.session.client_upload_id = record.session.client_upload_id;
    view.session.total_size = record.session.total_size;
    view.session.chunk_size = record.session.chunk_size;
    view.session.status = *upload_status;
    view.session.version = record.session.version;
    view.session.created_at = record.session.created_at;
    view.session.updated_at = record.session.updated_at;
    view.session.expires_at = record.session.expires_at;
    view.session.completed_at = record.session.completed_at;
    return view;
}

std::string AppendField(const std::string& value) {
    return std::to_string(value.size()) + ":" + value + "|";
}

std::string Sha256Hex(const std::string& input) {
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int digest_size = 0;
    if (EVP_Digest(
            input.data(), input.size(), digest, &digest_size,
            EVP_sha256(), nullptr
        ) != 1 || digest_size == 0) {
        return {};
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_size; ++i) {
        output << std::setw(2) << static_cast<unsigned int>(digest[i]);
    }
    return output.str();
}

std::string Fingerprint(const BeginUploadCommand& command) {
    std::string canonical = "begin_upload|";
    canonical += std::to_string(command.actor_user_id) + "|";
    canonical += AppendField(command.file_name);
    canonical += AppendField(command.content_type);
    canonical += std::to_string(command.total_size) + "|";
    canonical += AppendField(command.checksum_algorithm);
    canonical += AppendField(command.expected_checksum);
    canonical += std::to_string(command.preferred_chunk_size);
    return Sha256Hex(canonical);
}

std::string BuildStorageKey(std::uint64_t file_id) {
    return "files/" + std::to_string(file_id);
}

std::string BuildStoragePartKey(std::uint64_t upload_id, std::uint64_t chunk_index) {
    return "uploads/" + std::to_string(upload_id) + "/chunks/" +
           std::to_string(chunk_index) + ".part";
}

void RollbackIfNeeded(tinyimx::MySqlConnection* connection) {
    if (connection != nullptr && connection->InTransaction()) {
        connection->Rollback();
    }
}

BeginUploadResult BeginStorageFailure(FileApplicationStatus status, std::string message) {
    BeginUploadResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

GetUploadSessionResult GetStorageFailure(FileApplicationStatus status, std::string message) {
    GetUploadSessionResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

CancelUploadResult CancelStorageFailure(FileApplicationStatus status, std::string message) {
    CancelUploadResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

ReserveChunkResult ReserveFailure(FileApplicationStatus status, std::string message) {
    ReserveChunkResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

MarkChunkStoredResult MarkFailure(FileApplicationStatus status, std::string message) {
    MarkChunkStoredResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

BeginUploadResult ResolveExistingUpload(
    tinyimx::FileRepository* repository,
    std::uint64_t actor_user_id,
    const std::string& client_upload_id,
    const std::string& fingerprint
) {
    if (repository == nullptr) {
        return BeginStorageFailure(
            FileApplicationStatus::kStorageError,
            "file repository is unavailable"
        );
    }
    const auto existing = repository->FindByClientUploadId(actor_user_id, client_upload_id);
    if (!existing.Succeeded()) {
        return BeginStorageFailure(MapStorageStatus(existing.status), existing.message);
    }
    if (!existing.found) {
        return BeginStorageFailure(FileApplicationStatus::kNotFound, "upload operation not found");
    }
    const auto view = ToView(existing.record);
    if (!view.has_value()) {
        return BeginStorageFailure(
            FileApplicationStatus::kInvalidRecord,
            "upload operation points to invalid file/session records"
        );
    }

    BeginUploadResult result;
    result.status = FileApplicationStatus::kSucceeded;
    if (existing.record.session.request_fingerprint != fingerprint) {
        result.outcome = BeginUploadOutcome::kIdempotencyConflict;
        result.message = "client_upload_id was already used for a different file upload";
        return result;
    }
    result.outcome = BeginUploadOutcome::kReused;
    result.bundle = *view;
    result.message = "upload safely reused from durable session";
    return result;
}

}  // namespace

FileRepositoryAdapter::FileRepositoryAdapter(
    tinyimx::FileRepository* repository,
    tinyimx::MySqlConnectionPool* pool
)
    : repository_(repository), pool_(pool) {
}

BeginUploadResult FileRepositoryAdapter::BeginUpload(
    const BeginUploadCommand& command
) {
    if (repository_ == nullptr || pool_ == nullptr) {
        return BeginStorageFailure(
            FileApplicationStatus::kStorageError,
            "file repository adapter dependencies are unavailable"
        );
    }

    const std::string fingerprint = Fingerprint(command);
    if (fingerprint.size() != 64) {
        return BeginStorageFailure(
            FileApplicationStatus::kStorageError,
            "failed to fingerprint BeginUpload request"
        );
    }

    const auto precheck = repository_->FindByClientUploadId(
        command.actor_user_id, command.client_upload_id
    );
    if (!precheck.Succeeded()) {
        return BeginStorageFailure(MapStorageStatus(precheck.status), precheck.message);
    }
    if (precheck.found) {
        return ResolveExistingUpload(
            repository_, command.actor_user_id, command.client_upload_id, fingerprint
        );
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        return BeginStorageFailure(
            FileApplicationStatus::kStorageError,
            "BeginUpload failed to acquire database connection"
        );
    }
    if (!connection->BeginTransaction()) {
        return BeginStorageFailure(
            FileApplicationStatus::kStorageError,
            "BeginUpload failed to begin transaction: " + connection->LastError()
        );
    }

    const auto user_exists = repository_->UserExistsOnConnection(
        connection.operator->(), command.actor_user_id
    );
    if (!user_exists.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return BeginStorageFailure(MapStorageStatus(user_exists.status), user_exists.message);
    }
    if (!user_exists.value) {
        RollbackIfNeeded(connection.operator->());
        return BeginStorageFailure(
            FileApplicationStatus::kNotFound,
            "BeginUpload owner user does not exist"
        );
    }

    const auto inserted_file = repository_->InsertFileOnConnection(
        connection.operator->(), command.actor_user_id, command.file_name,
        command.content_type, command.total_size, command.checksum_algorithm,
        command.expected_checksum
    );
    if (!inserted_file.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return BeginStorageFailure(MapStorageStatus(inserted_file.status), inserted_file.message);
    }

    const auto storage_key = repository_->SetStorageKeyOnConnection(
        connection.operator->(), inserted_file.file_id, BuildStorageKey(inserted_file.file_id)
    );
    if (!storage_key.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return BeginStorageFailure(MapStorageStatus(storage_key.status), storage_key.message);
    }

    const auto inserted_session = repository_->InsertUploadSessionIdempotentOnConnection(
        connection.operator->(), inserted_file.file_id, command.actor_user_id,
        command.client_upload_id, fingerprint, command.total_size,
        command.preferred_chunk_size
    );
    if (!inserted_session.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return BeginStorageFailure(
            MapStorageStatus(inserted_session.status), inserted_session.message
        );
    }

    if (!inserted_session.inserted) {
        // A concurrent request won the unique (owner_user_id, client_upload_id)
        // race. Roll back our speculative file row before resolving that winner.
        RollbackIfNeeded(connection.operator->());
        connection.Reset();
        return ResolveExistingUpload(
            repository_, command.actor_user_id, command.client_upload_id, fingerprint
        );
    }

    const auto created = repository_->FindUploadBundleOnConnection(
        connection.operator->(), command.actor_user_id, inserted_session.upload_id, false
    );
    if (!created.Succeeded() || !created.found) {
        RollbackIfNeeded(connection.operator->());
        return BeginStorageFailure(
            created.Succeeded() ? FileApplicationStatus::kInvalidRecord
                                : MapStorageStatus(created.status),
            created.Succeeded() ? "new upload session could not be re-read" : created.message
        );
    }
    const auto created_view = ToView(created.record);
    if (!created_view.has_value()) {
        RollbackIfNeeded(connection.operator->());
        return BeginStorageFailure(
            FileApplicationStatus::kInvalidRecord,
            "new upload bundle is invalid"
        );
    }

    if (connection->Commit()) {
        BeginUploadResult result;
        result.status = FileApplicationStatus::kSucceeded;
        result.outcome = BeginUploadOutcome::kCreated;
        result.bundle = *created_view;
        result.message = "file metadata and upload session committed atomically";
        return result;
    }

    const std::string commit_error = connection->LastError();
    RollbackIfNeeded(connection.operator->());
    connection.Reset();

    // Commit outcome is ambiguous after a transport failure. Durable
    // idempotency is the recovery oracle, exactly as in M17 reliable writes.
    auto recovered = ResolveExistingUpload(
        repository_, command.actor_user_id, command.client_upload_id, fingerprint
    );
    if (recovered.status == FileApplicationStatus::kSucceeded &&
        recovered.outcome == BeginUploadOutcome::kReused) {
        recovered.message = "ambiguous commit recovered from durable upload session";
        return recovered;
    }

    std::string message = "BeginUpload transaction commit failed";
    if (!commit_error.empty()) {
        message += ": " + commit_error;
    }
    return BeginStorageFailure(FileApplicationStatus::kStorageError, std::move(message));
}

GetUploadSessionResult FileRepositoryAdapter::GetUploadSession(
    const GetUploadSessionQuery& query
) {
    if (repository_ == nullptr) {
        return GetStorageFailure(
            FileApplicationStatus::kStorageError,
            "file repository is unavailable"
        );
    }
    const auto found = repository_->FindUploadBundle(query.actor_user_id, query.upload_id);
    if (!found.Succeeded()) {
        return GetStorageFailure(MapStorageStatus(found.status), found.message);
    }
    if (!found.found) {
        return GetStorageFailure(FileApplicationStatus::kNotFound, "upload session not found");
    }
    const auto view = ToView(found.record);
    if (!view.has_value()) {
        return GetStorageFailure(
            FileApplicationStatus::kInvalidRecord,
            "upload session contains inconsistent durable records"
        );
    }
    GetUploadSessionResult result;
    result.status = FileApplicationStatus::kSucceeded;
    result.bundle = *view;
    result.message = "upload session found";
    return result;
}

CancelUploadResult FileRepositoryAdapter::CancelUpload(
    const CancelUploadCommand& command
) {
    if (repository_ == nullptr || pool_ == nullptr) {
        return CancelStorageFailure(
            FileApplicationStatus::kStorageError,
            "file repository adapter dependencies are unavailable"
        );
    }

    auto connection = pool_->Acquire();
    if (!connection) {
        return CancelStorageFailure(
            FileApplicationStatus::kStorageError,
            "CancelUpload failed to acquire database connection"
        );
    }
    if (!connection->BeginTransaction()) {
        return CancelStorageFailure(
            FileApplicationStatus::kStorageError,
            "CancelUpload failed to begin transaction: " + connection->LastError()
        );
    }

    const auto current = repository_->FindUploadBundleOnConnection(
        connection.operator->(), command.actor_user_id, command.upload_id, true
    );
    if (!current.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return CancelStorageFailure(MapStorageStatus(current.status), current.message);
    }
    if (!current.found) {
        RollbackIfNeeded(connection.operator->());
        return CancelStorageFailure(FileApplicationStatus::kNotFound, "upload session not found");
    }

    const auto current_view = ToView(current.record);
    if (!current_view.has_value()) {
        RollbackIfNeeded(connection.operator->());
        return CancelStorageFailure(
            FileApplicationStatus::kInvalidRecord,
            "upload session contains inconsistent durable records"
        );
    }

    if (current_view->session.status == UploadSessionStatus::kCanceled &&
        current_view->file.status == FileStatus::kCanceled) {
        RollbackIfNeeded(connection.operator->());
        CancelUploadResult result;
        result.status = FileApplicationStatus::kSucceeded;
        result.outcome = CancelUploadOutcome::kReused;
        result.bundle = *current_view;
        result.message = "CancelUpload safely reused canceled durable state";
        return result;
    }

    if (current_view->session.status != UploadSessionStatus::kActive ||
        current_view->file.status != FileStatus::kUploading) {
        RollbackIfNeeded(connection.operator->());
        return CancelStorageFailure(
            FileApplicationStatus::kFailedPrecondition,
            "only an active upload with UPLOADING file state can be canceled"
        );
    }

    const auto canceled = repository_->CancelUploadOnConnection(
        connection.operator->(), command.actor_user_id, command.upload_id,
        current_view->file.file_id
    );
    if (!canceled.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return CancelStorageFailure(MapStorageStatus(canceled.status), canceled.message);
    }

    const auto updated = repository_->FindUploadBundleOnConnection(
        connection.operator->(), command.actor_user_id, command.upload_id, false
    );
    if (!updated.Succeeded() || !updated.found) {
        RollbackIfNeeded(connection.operator->());
        return CancelStorageFailure(
            updated.Succeeded() ? FileApplicationStatus::kInvalidRecord
                                : MapStorageStatus(updated.status),
            updated.Succeeded() ? "canceled upload could not be re-read" : updated.message
        );
    }
    const auto updated_view = ToView(updated.record);
    if (!updated_view.has_value() ||
        updated_view->session.status != UploadSessionStatus::kCanceled ||
        updated_view->file.status != FileStatus::kCanceled) {
        RollbackIfNeeded(connection.operator->());
        return CancelStorageFailure(
            FileApplicationStatus::kInvalidRecord,
            "cancel transaction produced invalid file/session state"
        );
    }

    if (connection->Commit()) {
        CancelUploadResult result;
        result.status = FileApplicationStatus::kSucceeded;
        result.outcome = CancelUploadOutcome::kApplied;
        result.bundle = *updated_view;
        result.message = "upload canceled atomically";
        return result;
    }

    const std::string commit_error = connection->LastError();
    RollbackIfNeeded(connection.operator->());
    connection.Reset();

    const auto recovered = GetUploadSession({command.actor_user_id, command.upload_id});
    if (recovered.Found() &&
        recovered.bundle->session.status == UploadSessionStatus::kCanceled &&
        recovered.bundle->file.status == FileStatus::kCanceled) {
        CancelUploadResult result;
        result.status = FileApplicationStatus::kSucceeded;
        result.outcome = CancelUploadOutcome::kReused;
        result.bundle = recovered.bundle;
        result.message = "ambiguous cancel commit recovered from durable state";
        return result;
    }

    std::string message = "CancelUpload transaction commit failed";
    if (!commit_error.empty()) {
        message += ": " + commit_error;
    }
    return CancelStorageFailure(FileApplicationStatus::kStorageError, std::move(message));
}


ReserveChunkResult FileRepositoryAdapter::ReserveChunk(
    const ReserveChunkCommand& command
) {
    if (repository_ == nullptr || pool_ == nullptr) {
        return ReserveFailure(FileApplicationStatus::kStorageError,
                              "file repository adapter dependencies are unavailable");
    }
    auto connection = pool_->Acquire();
    if (!connection) {
        return ReserveFailure(FileApplicationStatus::kStorageError,
                              "ReserveChunk failed to acquire database connection");
    }
    if (!connection->BeginTransaction()) {
        return ReserveFailure(FileApplicationStatus::kStorageError,
                              "ReserveChunk failed to begin transaction: " + connection->LastError());
    }

    const auto bundle = repository_->FindUploadBundleOnConnection(
        connection.operator->(), command.actor_user_id, command.upload_id, true
    );
    if (!bundle.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return ReserveFailure(MapStorageStatus(bundle.status), bundle.message);
    }
    if (!bundle.found) {
        RollbackIfNeeded(connection.operator->());
        return ReserveFailure(FileApplicationStatus::kNotFound, "upload session not found");
    }
    const auto view = ToView(bundle.record);
    if (!view.has_value()) {
        RollbackIfNeeded(connection.operator->());
        return ReserveFailure(FileApplicationStatus::kInvalidRecord,
                              "upload session contains inconsistent durable records");
    }
    if (view->session.status != UploadSessionStatus::kActive ||
        view->file.status != FileStatus::kUploading) {
        RollbackIfNeeded(connection.operator->());
        return ReserveFailure(FileApplicationStatus::kFailedPrecondition,
                              "UploadChunk requires ACTIVE upload and UPLOADING file state");
    }
    if (view->session.chunk_size == 0 || view->session.total_size == 0) {
        RollbackIfNeeded(connection.operator->());
        return ReserveFailure(FileApplicationStatus::kInvalidRecord,
                              "upload session has invalid chunk geometry");
    }
    if (command.chunk_index > (UINT64_MAX / view->session.chunk_size)) {
        RollbackIfNeeded(connection.operator->());
        return ReserveFailure(FileApplicationStatus::kInvalidArgument, "chunk index overflows byte offset");
    }
    const std::uint64_t expected_offset = command.chunk_index * view->session.chunk_size;
    if (expected_offset >= view->session.total_size) {
        RollbackIfNeeded(connection.operator->());
        return ReserveFailure(FileApplicationStatus::kInvalidArgument, "chunk index is outside upload size");
    }
    const std::uint64_t remaining = view->session.total_size - expected_offset;
    const std::uint64_t expected_size = std::min(view->session.chunk_size, remaining);
    if (command.byte_offset != expected_offset || command.chunk_size != expected_size) {
        RollbackIfNeeded(connection.operator->());
        return ReserveFailure(FileApplicationStatus::kInvalidArgument,
                              "chunk offset/size does not match durable upload geometry");
    }

    const std::string storage_part_key = BuildStoragePartKey(command.upload_id, command.chunk_index);
    const auto inserted = repository_->InsertChunkIdempotentOnConnection(
        connection.operator->(), command.upload_id, command.chunk_index, view->file.file_id,
        command.actor_user_id, command.byte_offset, command.chunk_size,
        command.checksum_algorithm, command.checksum, storage_part_key
    );
    if (!inserted.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return ReserveFailure(MapStorageStatus(inserted.status), inserted.message);
    }

    const auto current = repository_->FindChunkOnConnection(
        connection.operator->(), command.actor_user_id, command.upload_id,
        command.chunk_index, true
    );
    if (!current.Succeeded() || !current.found) {
        RollbackIfNeeded(connection.operator->());
        return ReserveFailure(current.Succeeded() ? FileApplicationStatus::kInvalidRecord
                                                  : MapStorageStatus(current.status),
                              current.Succeeded() ? "reserved chunk could not be re-read" : current.message);
    }
    const auto chunk = ToChunkView(current.record);
    if (!chunk.has_value()) {
        RollbackIfNeeded(connection.operator->());
        return ReserveFailure(FileApplicationStatus::kInvalidRecord,
                              "reserved chunk contains invalid durable fields");
    }
    const bool same_identity =
        chunk->file_id == view->file.file_id &&
        chunk->owner_user_id == command.actor_user_id &&
        chunk->byte_offset == command.byte_offset &&
        chunk->chunk_size == command.chunk_size &&
        chunk->checksum_algorithm == command.checksum_algorithm &&
        chunk->checksum == command.checksum &&
        chunk->storage_part_key == storage_part_key;
    if (!same_identity) {
        RollbackIfNeeded(connection.operator->());
        ReserveChunkResult conflict;
        conflict.status = FileApplicationStatus::kSucceeded;
        conflict.outcome = ReserveChunkOutcome::kIdempotencyConflict;
        conflict.chunk = *chunk;
        conflict.message = "chunk_index was already reserved for different bytes or geometry";
        return conflict;
    }

    if (!connection->Commit()) {
        RollbackIfNeeded(connection.operator->());
        connection.Reset();
        const auto recovered = repository_->FindChunk(
            command.actor_user_id, command.upload_id, command.chunk_index
        );
        if (recovered.Found()) {
            const auto recovered_view = ToChunkView(recovered.record);
            if (recovered_view.has_value() && recovered_view->byte_offset == command.byte_offset &&
                recovered_view->chunk_size == command.chunk_size &&
                recovered_view->checksum == command.checksum) {
                ReserveChunkResult result;
                result.status = FileApplicationStatus::kSucceeded;
                result.outcome = ReserveChunkOutcome::kReused;
                result.chunk = *recovered_view;
                result.message = "ambiguous chunk reservation commit recovered from durable manifest";
                return result;
            }
        }
        return ReserveFailure(FileApplicationStatus::kStorageError,
                              "ReserveChunk transaction commit failed");
    }

    ReserveChunkResult result;
    result.status = FileApplicationStatus::kSucceeded;
    result.outcome = inserted.inserted ? ReserveChunkOutcome::kCreated : ReserveChunkOutcome::kReused;
    result.chunk = *chunk;
    result.message = inserted.inserted ? "chunk identity durably reserved"
                                       : "chunk identity safely reused";
    return result;
}

MarkChunkStoredResult FileRepositoryAdapter::MarkChunkStored(
    const MarkChunkStoredCommand& command
) {
    if (repository_ == nullptr || pool_ == nullptr) {
        return MarkFailure(FileApplicationStatus::kStorageError,
                           "file repository adapter dependencies are unavailable");
    }
    auto connection = pool_->Acquire();
    if (!connection) {
        return MarkFailure(FileApplicationStatus::kStorageError,
                           "MarkChunkStored failed to acquire database connection");
    }
    if (!connection->BeginTransaction()) {
        return MarkFailure(FileApplicationStatus::kStorageError,
                           "MarkChunkStored failed to begin transaction: " + connection->LastError());
    }
    const auto bundle = repository_->FindUploadBundleOnConnection(
        connection.operator->(), command.actor_user_id, command.upload_id, true
    );
    if (!bundle.Succeeded() || !bundle.found) {
        RollbackIfNeeded(connection.operator->());
        return MarkFailure(bundle.Succeeded() ? FileApplicationStatus::kNotFound
                                              : MapStorageStatus(bundle.status),
                           bundle.Succeeded() ? "upload session not found" : bundle.message);
    }
    const auto bundle_view = ToView(bundle.record);
    if (!bundle_view.has_value()) {
        RollbackIfNeeded(connection.operator->());
        return MarkFailure(FileApplicationStatus::kInvalidRecord,
                           "upload session contains inconsistent durable records");
    }
    if (bundle_view->session.status != UploadSessionStatus::kActive ||
        bundle_view->file.status != FileStatus::kUploading) {
        RollbackIfNeeded(connection.operator->());
        return MarkFailure(FileApplicationStatus::kFailedPrecondition,
                           "upload state changed before chunk storage commit");
    }

    const auto current = repository_->FindChunkOnConnection(
        connection.operator->(), command.actor_user_id, command.upload_id,
        command.chunk_index, true
    );
    if (!current.Succeeded() || !current.found) {
        RollbackIfNeeded(connection.operator->());
        return MarkFailure(current.Succeeded() ? FileApplicationStatus::kNotFound
                                               : MapStorageStatus(current.status),
                           current.Succeeded() ? "chunk reservation not found" : current.message);
    }
    const auto current_view = ToChunkView(current.record);
    if (!current_view.has_value() || current_view->chunk_size != command.chunk_size ||
        current_view->checksum != command.checksum) {
        RollbackIfNeeded(connection.operator->());
        return MarkFailure(FileApplicationStatus::kInvalidRecord,
                           "chunk storage commit does not match durable reservation");
    }
    if (current_view->status == UploadChunkStatus::kStored) {
        RollbackIfNeeded(connection.operator->());
        MarkChunkStoredResult result;
        result.status = FileApplicationStatus::kSucceeded;
        result.chunk = *current_view;
        result.message = "chunk was already durably stored";
        return result;
    }

    const auto mutation = repository_->MarkChunkStoredOnConnection(
        connection.operator->(), command.actor_user_id, command.upload_id,
        command.chunk_index, command.chunk_size, command.checksum
    );
    if (!mutation.Succeeded()) {
        RollbackIfNeeded(connection.operator->());
        return MarkFailure(MapStorageStatus(mutation.status), mutation.message);
    }
    const auto updated = repository_->FindChunkOnConnection(
        connection.operator->(), command.actor_user_id, command.upload_id,
        command.chunk_index, false
    );
    if (!updated.Succeeded() || !updated.found) {
        RollbackIfNeeded(connection.operator->());
        return MarkFailure(updated.Succeeded() ? FileApplicationStatus::kInvalidRecord
                                               : MapStorageStatus(updated.status),
                           updated.Succeeded() ? "stored chunk could not be re-read" : updated.message);
    }
    const auto updated_view = ToChunkView(updated.record);
    if (!updated_view.has_value() || updated_view->status != UploadChunkStatus::kStored) {
        RollbackIfNeeded(connection.operator->());
        return MarkFailure(FileApplicationStatus::kInvalidRecord,
                           "stored chunk durable state is invalid");
    }
    if (!connection->Commit()) {
        RollbackIfNeeded(connection.operator->());
        connection.Reset();
        const auto recovered = repository_->FindChunk(
            command.actor_user_id, command.upload_id, command.chunk_index
        );
        if (recovered.Found()) {
            const auto recovered_view = ToChunkView(recovered.record);
            if (recovered_view.has_value() && recovered_view->status == UploadChunkStatus::kStored &&
                recovered_view->checksum == command.checksum) {
                MarkChunkStoredResult result;
                result.status = FileApplicationStatus::kSucceeded;
                result.chunk = *recovered_view;
                result.message = "ambiguous stored commit recovered from durable manifest";
                return result;
            }
        }
        return MarkFailure(FileApplicationStatus::kStorageError,
                           "MarkChunkStored transaction commit failed");
    }

    MarkChunkStoredResult result;
    result.status = FileApplicationStatus::kSucceeded;
    result.chunk = *updated_view;
    result.message = "chunk storage completion committed";
    return result;
}

}  // namespace tinyimx::file
