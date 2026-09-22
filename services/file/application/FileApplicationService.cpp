#include "services/file/application/FileApplicationService.h"

#include "services/file/storage/FileStoragePort.h"

#include <openssl/evp.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>

namespace tinyimx::file {
namespace {

constexpr std::uint64_t kDefaultChunkSize = 4ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMinChunkSize = 256ULL * 1024ULL;
constexpr std::uint64_t kMaxChunkSize = 8ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kChunkAlignment = 64ULL * 1024ULL;

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

}  // namespace tinyimx::file
