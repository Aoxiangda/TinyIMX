#include "services/file/application/FileApplicationService.h"

#include <algorithm>
#include <cctype>
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
    if (value.empty() || value.size() > 255) {
        return false;
    }
    for (unsigned char ch : value) {
        if (ch < 0x20 || ch == 0x7f || ch == '/' || ch == '\\') {
            return false;
        }
    }
    return value != "." && value != "..";
}

bool ValidSha256Hex(const std::string& value) {
    if (value.size() != 64) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
}

bool ValidChunkSize(std::uint64_t value) {
    return value >= kMinChunkSize && value <= kMaxChunkSize &&
           value % kChunkAlignment == 0;
}

BeginUploadResult InvalidBegin(std::string message) {
    BeginUploadResult result;
    result.status = FileApplicationStatus::kInvalidArgument;
    result.message = std::move(message);
    return result;
}

}  // namespace

FileApplicationService::FileApplicationService(FileRepositoryPort* repository)
    : repository_(repository) {
}

BeginUploadResult FileApplicationService::BeginUpload(BeginUploadCommand command) {
    command.client_upload_id = Trim(std::move(command.client_upload_id));
    command.file_name = Trim(std::move(command.file_name));
    command.content_type = Lower(Trim(std::move(command.content_type)));
    command.checksum_algorithm = Lower(Trim(std::move(command.checksum_algorithm)));
    command.expected_checksum = Lower(Trim(std::move(command.expected_checksum)));

    if (command.actor_user_id == 0) {
        return InvalidBegin("actor_user_id must be non-zero");
    }
    if (command.client_upload_id.empty() || command.client_upload_id.size() > 64) {
        return InvalidBegin("client_upload_id must contain 1..64 characters");
    }
    if (!SafeMetadataFileName(command.file_name)) {
        return InvalidBegin("file_name is invalid or unsafe");
    }
    if (command.content_type.empty()) {
        command.content_type = "application/octet-stream";
    }
    if (command.content_type.size() > 128) {
        return InvalidBegin("content_type exceeds 128 characters");
    }
    if (command.total_size == 0) {
        return InvalidBegin("total_size must be greater than zero");
    }
    if (command.checksum_algorithm != "sha256" ||
        !ValidSha256Hex(command.expected_checksum)) {
        return InvalidBegin("M18-A1 requires a valid SHA-256 whole-file checksum");
    }
    if (command.preferred_chunk_size == 0) {
        command.preferred_chunk_size = kDefaultChunkSize;
    }
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

}  // namespace tinyimx::file
