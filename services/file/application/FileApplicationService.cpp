#include "services/file/application/FileApplicationService.h"

#include "services/file/storage/FileStoragePort.h"

#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

namespace tinyimx::file {
namespace {

constexpr std::uint64_t kDefaultChunkSize = 4ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMinChunkSize = 256ULL * 1024ULL;
constexpr std::uint64_t kMaxChunkSize = 8ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kChunkAlignment = 64ULL * 1024ULL;
constexpr std::uint64_t kMaxDownloadRangeSize = 1ULL * 1024ULL * 1024ULL;

std::string Trim(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool SafeMetadataFileName(const std::string& value) {
    if (value.empty() || value.size() > 255) return false;
    for (unsigned char ch : value) {
        if (ch < 0x20 || ch == 0x7f || ch == '/' || ch == '\\') return false;
    }
    return value != "." && value != "..";
}

bool ValidSha256Hex(const std::string& value) {
    if (value.size() != 64) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
}

bool ValidChunkSize(std::uint64_t value) {
    return value >= kMinChunkSize && value <= kMaxChunkSize &&
           value % kChunkAlignment == 0;
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

BeginUploadResult InvalidBegin(std::string message) {
    BeginUploadResult result;
    result.status = FileApplicationStatus::kInvalidArgument;
    result.message = std::move(message);
    return result;
}

UploadChunkResult InvalidChunk(std::string message) {
    UploadChunkResult result;
    result.status = FileApplicationStatus::kInvalidArgument;
    result.message = std::move(message);
    return result;
}

void AppendMissingRange(
    std::vector<ChunkIndexRange>* ranges,
    std::uint64_t start_index,
    std::uint64_t end_index
) {
    if (ranges == nullptr || start_index > end_index) return;
    if (!ranges->empty() && ranges->back().end_index != std::numeric_limits<std::uint64_t>::max() &&
        ranges->back().end_index + 1 == start_index) {
        ranges->back().end_index = end_index;
        return;
    }
    ranges->push_back({start_index, end_index});
}

std::optional<UploadProgressView> BuildProgress(const UploadSnapshotView& snapshot) {
    const auto& file = snapshot.bundle.file;
    const auto& session = snapshot.bundle.session;
    if (file.file_id == 0 || session.upload_id == 0 || file.file_id != session.file_id ||
        file.owner_user_id == 0 || file.owner_user_id != session.owner_user_id ||
        file.total_size == 0 || file.total_size != session.total_size || session.chunk_size == 0) {
        return std::nullopt;
    }

    UploadProgressView progress;
    progress.bundle = snapshot.bundle;
    progress.expected_chunk_count = 1 + ((session.total_size - 1) / session.chunk_size);

    std::uint64_t cursor = 0;
    bool first = true;
    std::uint64_t previous_index = 0;
    for (const auto& chunk : snapshot.chunks) {
        if ((!first && chunk.chunk_index <= previous_index) ||
            chunk.chunk_index >= progress.expected_chunk_count ||
            chunk.upload_id != session.upload_id || chunk.file_id != file.file_id ||
            chunk.owner_user_id != session.owner_user_id ||
            chunk.checksum_algorithm != "sha256" || chunk.checksum.size() != 64) {
            return std::nullopt;
        }
        const std::uint64_t expected_offset = chunk.chunk_index * session.chunk_size;
        const std::uint64_t remaining = session.total_size - expected_offset;
        const std::uint64_t expected_size = std::min(session.chunk_size, remaining);
        if (chunk.byte_offset != expected_offset || chunk.chunk_size != expected_size) {
            return std::nullopt;
        }

        if (cursor < chunk.chunk_index) {
            AppendMissingRange(&progress.missing_ranges, cursor, chunk.chunk_index - 1);
        }
        if (chunk.status == UploadChunkStatus::kStored) {
            ++progress.stored_chunk_count;
        } else if (chunk.status == UploadChunkStatus::kReserved) {
            ++progress.reserved_chunk_count;
            AppendMissingRange(&progress.missing_ranges, chunk.chunk_index, chunk.chunk_index);
        } else {
            return std::nullopt;
        }
        cursor = chunk.chunk_index + 1;
        previous_index = chunk.chunk_index;
        first = false;
    }

    if (cursor < progress.expected_chunk_count) {
        AppendMissingRange(
            &progress.missing_ranges, cursor, progress.expected_chunk_count - 1
        );
    }

    const bool mutable_or_finalizing =
        (session.status == UploadSessionStatus::kActive && file.status == FileStatus::kUploading) ||
        (session.status == UploadSessionStatus::kFinalizing && file.status == FileStatus::kVerifying);
    progress.ready_to_finalize = mutable_or_finalizing && progress.missing_ranges.empty() &&
        progress.stored_chunk_count == progress.expected_chunk_count;
    return progress;
}

GetUploadProgressResult ProgressFailure(FileApplicationStatus status, std::string message) {
    GetUploadProgressResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

FinalizeUploadResult FinalizeFailure(FileApplicationStatus status, std::string message) {
    FinalizeUploadResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

GetDownloadInfoResult DownloadInfoFailure(
    FileApplicationStatus status,
    std::string message
) {
    GetDownloadInfoResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

ReadFileRangeResult RangeFailure(
    FileApplicationStatus status,
    std::string message
) {
    ReadFileRangeResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

std::optional<DownloadInfoView> BuildDownloadInfo(const FileView& file) {
    if (file.file_id == 0 || file.owner_user_id == 0 || file.total_size == 0 ||
        file.status != FileStatus::kAvailable || file.checksum_algorithm != "sha256" ||
        !ValidSha256Hex(file.expected_checksum) || !ValidSha256Hex(file.verified_checksum) ||
        file.expected_checksum != file.verified_checksum || file.storage_backend.empty() ||
        file.storage_key.empty() || file.version == 0 || file.available_at.empty()) {
        return std::nullopt;
    }
    DownloadInfoView info;
    info.file_id = file.file_id;
    info.file_name = file.file_name;
    info.content_type = file.content_type;
    info.total_size = file.total_size;
    info.checksum_algorithm = file.checksum_algorithm;
    info.verified_checksum = file.verified_checksum;
    info.version = file.version;
    info.available_at = file.available_at;
    return info;
}

}  // namespace

FileApplicationService::FileApplicationService(
    FileRepositoryPort* repository,
    FileStoragePort* storage
)
    : repository_(repository), storage_(storage) {
}

BeginUploadResult FileApplicationService::BeginUpload(BeginUploadCommand command) {
    command.client_upload_id = Trim(std::move(command.client_upload_id));
    command.file_name = Trim(std::move(command.file_name));
    command.content_type = Lower(Trim(std::move(command.content_type)));
    command.checksum_algorithm = Lower(Trim(std::move(command.checksum_algorithm)));
    command.expected_checksum = Lower(Trim(std::move(command.expected_checksum)));

    if (command.actor_user_id == 0) return InvalidBegin("actor_user_id must be non-zero");
    if (command.client_upload_id.empty() || command.client_upload_id.size() > 64) {
        return InvalidBegin("client_upload_id must contain 1..64 characters");
    }
    if (!SafeMetadataFileName(command.file_name)) {
        return InvalidBegin("file_name is invalid or unsafe");
    }
    if (command.content_type.empty()) command.content_type = "application/octet-stream";
    if (command.content_type.size() > 128) {
        return InvalidBegin("content_type exceeds 128 characters");
    }
    if (command.total_size == 0) return InvalidBegin("total_size must be greater than zero");
    if (command.checksum_algorithm != "sha256" ||
        !ValidSha256Hex(command.expected_checksum)) {
        return InvalidBegin("M18-A1 requires a valid SHA-256 whole-file checksum");
    }
    if (command.preferred_chunk_size == 0) command.preferred_chunk_size = kDefaultChunkSize;
    if (!ValidChunkSize(command.preferred_chunk_size)) {
        return InvalidBegin(
            "preferred_chunk_size must be 256KiB..8MiB and aligned to 64KiB"
        );
    }
    if (repository_ == nullptr) {
        BeginUploadResult result;
        result.status = FileApplicationStatus::kStorageError;
        result.message = "file repository port is unavailable";
        return result;
    }
    return repository_->BeginUpload(command);
}

GetUploadSessionResult FileApplicationService::GetUploadSession(
    const GetUploadSessionQuery& query
) {
    if (query.actor_user_id == 0 || query.upload_id == 0) {
        GetUploadSessionResult result;
        result.status = FileApplicationStatus::kInvalidArgument;
        result.message = "actor_user_id and upload_id must be non-zero";
        return result;
    }
    if (repository_ == nullptr) {
        GetUploadSessionResult result;
        result.status = FileApplicationStatus::kStorageError;
        result.message = "file repository port is unavailable";
        return result;
    }
    return repository_->GetUploadSession(query);
}

CancelUploadResult FileApplicationService::CancelUpload(
    const CancelUploadCommand& command
) {
    if (command.actor_user_id == 0 || command.upload_id == 0) {
        CancelUploadResult result;
        result.status = FileApplicationStatus::kInvalidArgument;
        result.message = "actor_user_id and upload_id must be non-zero";
        return result;
    }
    if (repository_ == nullptr) {
        CancelUploadResult result;
        result.status = FileApplicationStatus::kStorageError;
        result.message = "file repository port is unavailable";
        return result;
    }
    return repository_->CancelUpload(command);
}

UploadChunkResult FileApplicationService::UploadChunk(UploadChunkCommand command) {
    command.checksum_algorithm = Lower(Trim(std::move(command.checksum_algorithm)));
    command.checksum = Lower(Trim(std::move(command.checksum)));

    if (command.actor_user_id == 0 || command.upload_id == 0) {
        return InvalidChunk("actor_user_id and upload_id must be non-zero");
    }
    if (command.data.empty()) return InvalidChunk("chunk data must not be empty");
    if (command.data.size() > kMaxChunkSize) {
        return InvalidChunk("chunk data exceeds the M18-B1 8MiB chunk limit");
    }
    if (command.checksum_algorithm != "sha256" || !ValidSha256Hex(command.checksum)) {
        return InvalidChunk("UploadChunk requires a valid SHA-256 chunk checksum");
    }

    const std::string actual_checksum = Sha256Hex(command.data);
    if (actual_checksum.empty()) {
        UploadChunkResult result;
        result.status = FileApplicationStatus::kStorageError;
        result.message = "failed to compute chunk SHA-256";
        return result;
    }
    if (actual_checksum != command.checksum) {
        return InvalidChunk("chunk payload SHA-256 does not match request checksum");
    }
    if (repository_ == nullptr || storage_ == nullptr) {
        UploadChunkResult result;
        result.status = FileApplicationStatus::kStorageError;
        result.message = "chunk repository/storage dependency is unavailable";
        return result;
    }

    ReserveChunkCommand reserve_command;
    reserve_command.actor_user_id = command.actor_user_id;
    reserve_command.upload_id = command.upload_id;
    reserve_command.chunk_index = command.chunk_index;
    reserve_command.byte_offset = command.byte_offset;
    reserve_command.chunk_size = static_cast<std::uint64_t>(command.data.size());
    reserve_command.checksum_algorithm = command.checksum_algorithm;
    reserve_command.checksum = command.checksum;

    const auto reserved = repository_->ReserveChunk(reserve_command);
    if (!reserved.Accepted()) {
        UploadChunkResult result;
        result.status = reserved.status;
        result.outcome = reserved.outcome == ReserveChunkOutcome::kIdempotencyConflict
            ? UploadChunkOutcome::kIdempotencyConflict
            : UploadChunkOutcome::kReused;
        result.chunk = reserved.chunk;
        result.message = reserved.message;
        return result;
    }

    StoreChunkRequest store_request;
    store_request.storage_part_key = reserved.chunk->storage_part_key;
    store_request.data = command.data;
    store_request.expected_sha256 = command.checksum;
    const auto stored = storage_->StoreChunkAtomically(store_request);
    if (!stored.Succeeded() || stored.bytes_written != command.data.size() ||
        stored.verified_sha256 != command.checksum) {
        UploadChunkResult result;
        result.status = stored.status == FileStorageStatus::kInvalidArgument
            ? FileApplicationStatus::kInvalidArgument
            : FileApplicationStatus::kStorageError;
        result.outcome = reserved.outcome == ReserveChunkOutcome::kCreated
            ? UploadChunkOutcome::kStored
            : UploadChunkOutcome::kReused;
        result.chunk = reserved.chunk;
        result.message = stored.message.empty() ? "chunk storage write failed" : stored.message;
        return result;
    }

    MarkChunkStoredCommand mark_command;
    mark_command.actor_user_id = command.actor_user_id;
    mark_command.upload_id = command.upload_id;
    mark_command.chunk_index = command.chunk_index;
    mark_command.chunk_size = static_cast<std::uint64_t>(command.data.size());
    mark_command.checksum = command.checksum;
    const auto marked = repository_->MarkChunkStored(mark_command);
    if (!marked.Completed()) {
        UploadChunkResult result;
        result.status = marked.status;
        result.outcome = reserved.outcome == ReserveChunkOutcome::kCreated
            ? UploadChunkOutcome::kStored
            : UploadChunkOutcome::kReused;
        result.chunk = marked.chunk.has_value() ? marked.chunk : reserved.chunk;
        result.message = marked.message;
        return result;
    }

    UploadChunkResult result;
    result.status = FileApplicationStatus::kSucceeded;
    result.outcome = reserved.outcome == ReserveChunkOutcome::kCreated
        ? UploadChunkOutcome::kStored
        : UploadChunkOutcome::kReused;
    result.chunk = marked.chunk;
    result.message = reserved.outcome == ReserveChunkOutcome::kCreated
        ? "chunk durably stored"
        : "chunk retry safely reused durable identity";
    return result;
}

GetUploadProgressResult FileApplicationService::GetUploadProgress(
    const GetUploadProgressQuery& query
) {
    if (query.actor_user_id == 0 || query.upload_id == 0) {
        return ProgressFailure(
            FileApplicationStatus::kInvalidArgument,
            "actor_user_id and upload_id must be non-zero"
        );
    }
    if (repository_ == nullptr) {
        return ProgressFailure(
            FileApplicationStatus::kStorageError,
            "file repository port is unavailable"
        );
    }

    const auto snapshot = repository_->GetUploadSnapshot(query);
    if (!snapshot.Found()) {
        return ProgressFailure(snapshot.status, snapshot.message);
    }
    const auto progress = BuildProgress(*snapshot.snapshot);
    if (!progress.has_value()) {
        return ProgressFailure(
            FileApplicationStatus::kInvalidRecord,
            "upload snapshot violates durable chunk geometry"
        );
    }

    GetUploadProgressResult result;
    result.status = FileApplicationStatus::kSucceeded;
    result.progress = *progress;
    result.message = progress->ready_to_finalize
        ? "all durable chunks are stored"
        : "upload progress derived from durable chunk manifest";
    return result;
}

FinalizeUploadResult FileApplicationService::FinalizeUpload(
    const FinalizeUploadCommand& command
) {
    if (command.actor_user_id == 0 || command.upload_id == 0) {
        return FinalizeFailure(
            FileApplicationStatus::kInvalidArgument,
            "actor_user_id and upload_id must be non-zero"
        );
    }
    if (repository_ == nullptr || storage_ == nullptr) {
        return FinalizeFailure(
            FileApplicationStatus::kStorageError,
            "finalize repository/storage dependency is unavailable"
        );
    }

    const auto prepared = repository_->PrepareFinalize(command);
    if (!prepared.Completed()) {
        return FinalizeFailure(prepared.status, prepared.message);
    }
    if (!prepared.snapshot.has_value()) {
        return FinalizeFailure(
            FileApplicationStatus::kInvalidRecord,
            "FinalizeUpload preparation returned no durable snapshot"
        );
    }

    const auto progress = BuildProgress(*prepared.snapshot);
    if (!progress.has_value()) {
        return FinalizeFailure(
            FileApplicationStatus::kInvalidRecord,
            "FinalizeUpload snapshot violates durable chunk geometry"
        );
    }

    if (prepared.outcome == FinalizePreparationOutcome::kNotReady) {
        FinalizeUploadResult result;
        result.status = FileApplicationStatus::kSucceeded;
        result.outcome = FinalizeUploadOutcome::kNotReady;
        result.progress = *progress;
        result.bundle = prepared.snapshot->bundle;
        result.message = "FinalizeUpload requires every expected chunk to be STORED";
        return result;
    }
    if (prepared.outcome == FinalizePreparationOutcome::kChecksumMismatch) {
        FinalizeUploadResult result;
        result.status = FileApplicationStatus::kSucceeded;
        result.outcome = FinalizeUploadOutcome::kChecksumMismatch;
        result.progress = *progress;
        result.bundle = prepared.snapshot->bundle;
        result.verified_checksum = prepared.snapshot->bundle.file.verified_checksum;
        result.message = "upload is durably failed because whole-file SHA-256 mismatched";
        return result;
    }
    if (prepared.outcome == FinalizePreparationOutcome::kAlreadyCompleted) {
        FinalizeUploadResult result;
        result.status = FileApplicationStatus::kSucceeded;
        result.outcome = FinalizeUploadOutcome::kReused;
        result.progress = *progress;
        result.bundle = prepared.snapshot->bundle;
        result.verified_checksum = prepared.snapshot->bundle.file.verified_checksum;
        result.message = "FinalizeUpload safely reused AVAILABLE durable state";
        return result;
    }
    if (!progress->ready_to_finalize) {
        return FinalizeFailure(
            FileApplicationStatus::kInvalidRecord,
            "FINALIZING upload no longer has a complete STORED manifest"
        );
    }

    ComposeObjectRequest compose_request;
    compose_request.storage_key = prepared.snapshot->bundle.file.storage_key;
    compose_request.expected_total_size = prepared.snapshot->bundle.file.total_size;
    compose_request.expected_sha256 = prepared.snapshot->bundle.file.expected_checksum;
    compose_request.parts.reserve(prepared.snapshot->chunks.size());
    for (const auto& chunk : prepared.snapshot->chunks) {
        if (chunk.status != UploadChunkStatus::kStored) {
            return FinalizeFailure(
                FileApplicationStatus::kInvalidRecord,
                "FinalizeUpload encountered non-STORED chunk after preparation"
            );
        }
        compose_request.parts.push_back({
            chunk.storage_part_key, chunk.chunk_size, chunk.checksum
        });
    }

    const auto composed = storage_->ComposeObjectAtomically(compose_request);
    if (composed.status == FileStorageStatus::kChecksumMismatch) {
        const auto failed = repository_->FailFinalizeChecksum({
            command.actor_user_id, command.upload_id, composed.verified_sha256
        });
        if (!failed.Completed()) {
            return FinalizeFailure(failed.status, failed.message);
        }
        FinalizeUploadResult result;
        result.status = FileApplicationStatus::kSucceeded;
        result.outcome = FinalizeUploadOutcome::kChecksumMismatch;
        auto final_progress = *progress;
        if (failed.bundle.has_value()) {
            final_progress.bundle = *failed.bundle;
            final_progress.ready_to_finalize = false;
        }
        result.progress = std::move(final_progress);
        result.bundle = failed.bundle;
        result.verified_checksum = composed.verified_sha256;
        result.message = "whole-file SHA-256 mismatch recorded as terminal FAILED file state";
        return result;
    }
    if (!composed.Succeeded() ||
        composed.bytes_written != prepared.snapshot->bundle.file.total_size ||
        composed.verified_sha256 != prepared.snapshot->bundle.file.expected_checksum) {
        return FinalizeFailure(
            composed.status == FileStorageStatus::kInvalidArgument
                ? FileApplicationStatus::kInvalidArgument
                : FileApplicationStatus::kStorageError,
            composed.message.empty() ? "final object assembly failed" : composed.message
        );
    }

    const auto completed = repository_->CompleteFinalize({
        command.actor_user_id, command.upload_id, composed.verified_sha256
    });
    if (!completed.Completed()) {
        return FinalizeFailure(completed.status, completed.message);
    }

    FinalizeUploadResult result;
    result.status = FileApplicationStatus::kSucceeded;
    result.outcome = completed.outcome == CompleteFinalizeOutcome::kCompleted
        ? FinalizeUploadOutcome::kCompleted
        : FinalizeUploadOutcome::kReused;
    result.bundle = completed.bundle;
    result.verified_checksum = composed.verified_sha256;
    auto final_progress = *progress;
    if (completed.bundle.has_value()) {
        final_progress.bundle = *completed.bundle;
        final_progress.ready_to_finalize = false;
    }
    result.progress = std::move(final_progress);
    result.message = completed.outcome == CompleteFinalizeOutcome::kCompleted
        ? "final object atomically published and durable state marked AVAILABLE"
        : "FinalizeUpload response-loss retry reused COMPLETED/AVAILABLE state";
    return result;
}

GetDownloadInfoResult FileApplicationService::GetDownloadInfo(
    const GetDownloadInfoQuery& query
) {
    if (query.actor_user_id == 0 || query.file_id == 0) {
        return DownloadInfoFailure(
            FileApplicationStatus::kInvalidArgument,
            "actor_user_id and file_id must be non-zero"
        );
    }
    if (repository_ == nullptr) {
        return DownloadInfoFailure(
            FileApplicationStatus::kStorageError,
            "file repository port is unavailable"
        );
    }

    const auto found = repository_->GetDownloadFile(query);
    if (!found.Found()) {
        return DownloadInfoFailure(found.status, found.message);
    }
    const auto& file = *found.file;
    if (file.file_id != query.file_id || file.owner_user_id != query.actor_user_id) {
        return DownloadInfoFailure(
            FileApplicationStatus::kInvalidRecord,
            "download repository returned mismatched file ownership"
        );
    }
    if (file.status != FileStatus::kAvailable) {
        return DownloadInfoFailure(
            FileApplicationStatus::kFailedPrecondition,
            "only AVAILABLE files may be downloaded"
        );
    }
    const auto info = BuildDownloadInfo(file);
    if (!info.has_value()) {
        return DownloadInfoFailure(
            FileApplicationStatus::kInvalidRecord,
            "AVAILABLE file metadata/checksum invariants are invalid"
        );
    }

    GetDownloadInfoResult result;
    result.status = FileApplicationStatus::kSucceeded;
    result.info = *info;
    result.message = "download metadata authorized";
    return result;
}

ReadFileRangeResult FileApplicationService::ReadFileRange(ReadFileRangeQuery query) {
    query.if_match_sha256 = Lower(Trim(std::move(query.if_match_sha256)));
    if (query.actor_user_id == 0 || query.file_id == 0) {
        return RangeFailure(
            FileApplicationStatus::kInvalidArgument,
            "actor_user_id and file_id must be non-zero"
        );
    }
    if (query.length == 0 || query.length > kMaxDownloadRangeSize) {
        return RangeFailure(
            FileApplicationStatus::kInvalidArgument,
            "download range length must be 1..1MiB"
        );
    }
    if (!query.if_match_sha256.empty() && !ValidSha256Hex(query.if_match_sha256)) {
        return RangeFailure(
            FileApplicationStatus::kInvalidArgument,
            "if_match_sha256 must be empty or a 64-character SHA-256 digest"
        );
    }
    if (repository_ == nullptr || storage_ == nullptr) {
        return RangeFailure(
            FileApplicationStatus::kStorageError,
            "download repository/storage dependency is unavailable"
        );
    }

    const auto found = repository_->GetDownloadFile({query.actor_user_id, query.file_id});
    if (!found.Found()) {
        return RangeFailure(found.status, found.message);
    }
    const auto& file = *found.file;
    if (file.file_id != query.file_id || file.owner_user_id != query.actor_user_id) {
        return RangeFailure(
            FileApplicationStatus::kInvalidRecord,
            "download repository returned mismatched file ownership"
        );
    }
    if (file.status != FileStatus::kAvailable) {
        return RangeFailure(
            FileApplicationStatus::kFailedPrecondition,
            "only AVAILABLE files may be downloaded"
        );
    }
    const auto info = BuildDownloadInfo(file);
    if (!info.has_value()) {
        return RangeFailure(
            FileApplicationStatus::kInvalidRecord,
            "AVAILABLE file metadata/checksum invariants are invalid"
        );
    }
    if (!query.if_match_sha256.empty() && query.if_match_sha256 != info->verified_checksum) {
        return RangeFailure(
            FileApplicationStatus::kFailedPrecondition,
            "download validator no longer matches the AVAILABLE object"
        );
    }
    if (query.offset >= info->total_size) {
        return RangeFailure(
            FileApplicationStatus::kOutOfRange,
            "download offset must be smaller than total_size"
        );
    }
    const std::uint64_t effective_length = std::min(
        query.length, info->total_size - query.offset
    );

    const auto read = storage_->ReadObjectRange({
        file.storage_key, query.offset, effective_length, info->total_size
    });
    if (!read.Succeeded()) {
        if (read.status == FileStorageStatus::kInvalidArgument) {
            return RangeFailure(FileApplicationStatus::kInvalidArgument, read.message);
        }
        if (read.status == FileStorageStatus::kNotFound ||
            read.status == FileStorageStatus::kDataLoss) {
            return RangeFailure(
                FileApplicationStatus::kInvalidRecord,
                read.message.empty() ? "AVAILABLE storage object is missing or corrupt" : read.message
            );
        }
        return RangeFailure(
            FileApplicationStatus::kStorageError,
            read.message.empty() ? "storage range read failed" : read.message
        );
    }

    const std::uint64_t bytes_read = static_cast<std::uint64_t>(read.data.size());
    const std::string actual_range_sha256 = Sha256Hex(read.data);
    if (read.offset != query.offset || bytes_read != effective_length || bytes_read == 0 ||
        !ValidSha256Hex(read.range_sha256) || actual_range_sha256.empty() ||
        actual_range_sha256 != read.range_sha256 ||
        read.eof != (query.offset + bytes_read == info->total_size)) {
        return RangeFailure(
            FileApplicationStatus::kInvalidRecord,
            "storage range result violates immutable object invariants"
        );
    }

    ReadFileRangeResult result;
    result.status = FileApplicationStatus::kSucceeded;
    result.info = *info;
    result.offset = query.offset;
    result.data = read.data;
    result.range_sha256 = read.range_sha256;
    result.eof = read.eof;
    result.next_offset = query.offset + bytes_read;
    result.message = result.eof
        ? "final download range read"
        : "download range read; resume from next_offset after reconnect";
    return result;
}

}  // namespace tinyimx::file
