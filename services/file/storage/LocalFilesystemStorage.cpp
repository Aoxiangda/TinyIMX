#include "services/file/storage/LocalFilesystemStorage.h"

#include <openssl/evp.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <memory>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>

namespace tinyimx::file {
namespace {

std::atomic<std::uint64_t> g_temp_sequence{0};

struct EvpMdCtxDeleter {
    void operator()(EVP_MD_CTX* context) const noexcept {
        if (context != nullptr) EVP_MD_CTX_free(context);
    }
};
using EvpMdCtx = std::unique_ptr<EVP_MD_CTX, EvpMdCtxDeleter>;

std::string HexDigest(const unsigned char* digest, unsigned int digest_size) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_size; ++i) {
        output << std::setw(2) << static_cast<unsigned int>(digest[i]);
    }
    return output.str();
}

bool ValidSha256Hex(const std::string& value) {
    if (value.size() != 64) return false;
    for (unsigned char ch : value) {
        if (std::isxdigit(ch) == 0) return false;
    }
    return true;
}

std::string Sha256Hex(std::string_view data) {
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int digest_size = 0;
    if (EVP_Digest(
            data.data(), data.size(), digest, &digest_size,
            EVP_sha256(), nullptr
        ) != 1 || digest_size == 0) {
        return {};
    }
    return HexDigest(digest, digest_size);
}

StoreChunkResult InvalidChunk(std::string message) {
    StoreChunkResult result;
    result.status = FileStorageStatus::kInvalidArgument;
    result.message = std::move(message);
    return result;
}

StoreChunkResult ChunkIoFailure(std::string message) {
    StoreChunkResult result;
    result.status = FileStorageStatus::kIoError;
    result.message = std::move(message);
    return result;
}

ComposeObjectResult InvalidCompose(std::string message) {
    ComposeObjectResult result;
    result.status = FileStorageStatus::kInvalidArgument;
    result.message = std::move(message);
    return result;
}

ComposeObjectResult ComposeIoFailure(std::string message) {
    ComposeObjectResult result;
    result.status = FileStorageStatus::kIoError;
    result.message = std::move(message);
    return result;
}

ReadObjectRangeResult RangeFailure(
    FileStorageStatus status,
    std::string message
) {
    ReadObjectRangeResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

bool WriteAll(int fd, const char* data, std::size_t size, std::string* error) {
    std::size_t written = 0;
    while (written < size) {
        const ssize_t rc = ::write(fd, data + written, size - written);
        if (rc < 0) {
            if (errno == EINTR) continue;
            if (error != nullptr) {
                *error = std::string("write failed: ") + std::strerror(errno);
            }
            return false;
        }
        if (rc == 0) {
            if (error != nullptr) *error = "write returned zero bytes";
            return false;
        }
        written += static_cast<std::size_t>(rc);
    }
    return true;
}

bool WriteAll(int fd, std::string_view data, std::string* error) {
    return WriteAll(fd, data.data(), data.size(), error);
}

void RemoveNoThrow(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void FsyncDirectoryNoThrow(const std::filesystem::path& path) {
    const int directory_fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd >= 0) {
        (void)::fsync(directory_fd);
        (void)::close(directory_fd);
    }
}

std::filesystem::path TemporarySibling(const std::filesystem::path& destination) {
    const std::uint64_t sequence = g_temp_sequence.fetch_add(1, std::memory_order_relaxed);
    return destination.string() + ".tmp." +
        std::to_string(static_cast<unsigned long long>(::getpid())) + "." +
        std::to_string(sequence);
}

}  // namespace

LocalFilesystemStorage::LocalFilesystemStorage(std::filesystem::path root)
    : root_(std::move(root)) {
    if (!root_.empty()) root_ = root_.lexically_normal();
}

bool LocalFilesystemStorage::ResolveSafePath(
    const std::string& storage_part_key,
    std::filesystem::path* output
) const {
    if (output == nullptr || root_.empty() || storage_part_key.empty()) return false;
    const std::filesystem::path relative(storage_part_key);
    if (relative.is_absolute()) return false;
    for (const auto& component : relative) {
        const std::string text = component.string();
        if (text.empty() || text == "." || text == ".." ||
            text.find('\\') != std::string::npos) {
            return false;
        }
    }
    *output = (root_ / relative).lexically_normal();
    return true;
}

StoreChunkResult LocalFilesystemStorage::StoreChunkAtomically(
    const StoreChunkRequest& request
) {
    if (request.data.empty()) return InvalidChunk("chunk data must not be empty");
    if (!ValidSha256Hex(request.expected_sha256)) {
        return InvalidChunk("expected chunk SHA-256 must contain 64 hex characters");
    }
    const std::string actual_sha256 = Sha256Hex(request.data);
    if (actual_sha256.empty()) return ChunkIoFailure("failed to compute chunk SHA-256");
    if (actual_sha256 != request.expected_sha256) {
        return InvalidChunk("chunk payload does not match expected SHA-256");
    }

    std::filesystem::path destination;
    if (!ResolveSafePath(request.storage_part_key, &destination)) {
        return InvalidChunk("storage_part_key is unsafe");
    }

    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) return ChunkIoFailure("create chunk directory failed: " + ec.message());

    const std::filesystem::path temporary = TemporarySibling(destination);
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        return ChunkIoFailure(std::string("open temporary chunk failed: ") + std::strerror(errno));
    }

    std::string write_error;
    bool ok = WriteAll(fd, request.data, &write_error);
    if (ok && ::fsync(fd) != 0) {
        ok = false;
        write_error = std::string("fsync chunk failed: ") + std::strerror(errno);
    }
    if (::close(fd) != 0 && ok) {
        ok = false;
        write_error = std::string("close chunk failed: ") + std::strerror(errno);
    }
    if (!ok) {
        RemoveNoThrow(temporary);
        return ChunkIoFailure(std::move(write_error));
    }

    if (::rename(temporary.c_str(), destination.c_str()) != 0) {
        const std::string error = std::string("atomic chunk rename failed: ") +
            std::strerror(errno);
        RemoveNoThrow(temporary);
        return ChunkIoFailure(error);
    }
    FsyncDirectoryNoThrow(destination.parent_path());

    StoreChunkResult result;
    result.status = FileStorageStatus::kSucceeded;
    result.bytes_written = static_cast<std::uint64_t>(request.data.size());
    result.verified_sha256 = actual_sha256;
    result.message = "chunk atomically stored";
    return result;
}

ComposeObjectResult LocalFilesystemStorage::ComposeObjectAtomically(
    const ComposeObjectRequest& request
) {
    if (request.expected_total_size == 0 || request.parts.empty()) {
        return InvalidCompose("final object requires non-empty parts and total size");
    }
    if (!ValidSha256Hex(request.expected_sha256)) {
        return InvalidCompose("expected whole-file SHA-256 must contain 64 hex characters");
    }

    std::filesystem::path destination;
    if (!ResolveSafePath(request.storage_key, &destination)) {
        return InvalidCompose("storage_key is unsafe");
    }

    std::vector<std::filesystem::path> part_paths;
    part_paths.reserve(request.parts.size());
    for (const auto& part : request.parts) {
        if (part.expected_size == 0 || !ValidSha256Hex(part.expected_sha256)) {
            return InvalidCompose("final object contains invalid chunk descriptor");
        }
        std::filesystem::path part_path;
        if (!ResolveSafePath(part.storage_part_key, &part_path)) {
            return InvalidCompose("final object contains unsafe storage_part_key");
        }
        if (part_path == destination) {
            return InvalidCompose("final object cannot overwrite one of its source parts");
        }
        part_paths.push_back(std::move(part_path));
    }

    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) return ComposeIoFailure("create final-object directory failed: " + ec.message());

    const std::filesystem::path temporary = TemporarySibling(destination);
    const int output_fd = ::open(
        temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600
    );
    if (output_fd < 0) {
        return ComposeIoFailure(std::string("open temporary final object failed: ") +
                                std::strerror(errno));
    }

    EvpMdCtx whole_context(EVP_MD_CTX_new());
    if (!whole_context || EVP_DigestInit_ex(whole_context.get(), EVP_sha256(), nullptr) != 1) {
        (void)::close(output_fd);
        RemoveNoThrow(temporary);
        return ComposeIoFailure("initialize whole-file SHA-256 failed");
    }

    std::array<char, 64 * 1024> buffer{};
    std::uint64_t total_bytes = 0;
    std::string failure;

    for (std::size_t index = 0; index < request.parts.size() && failure.empty(); ++index) {
        const auto& descriptor = request.parts[index];
        const auto& part_path = part_paths[index];
        const int input_fd = ::open(part_path.c_str(), O_RDONLY | O_CLOEXEC);
        if (input_fd < 0) {
            failure = "open stored chunk failed for " + descriptor.storage_part_key + ": " +
                std::strerror(errno);
            break;
        }

        EvpMdCtx part_context(EVP_MD_CTX_new());
        if (!part_context || EVP_DigestInit_ex(part_context.get(), EVP_sha256(), nullptr) != 1) {
            (void)::close(input_fd);
            failure = "initialize chunk SHA-256 failed for " + descriptor.storage_part_key;
            break;
        }

        std::uint64_t part_bytes = 0;
        while (failure.empty()) {
            const ssize_t read_bytes = ::read(input_fd, buffer.data(), buffer.size());
            if (read_bytes < 0) {
                if (errno == EINTR) continue;
                failure = "read stored chunk failed for " + descriptor.storage_part_key + ": " +
                    std::strerror(errno);
                break;
            }
            if (read_bytes == 0) break;
            const std::size_t size = static_cast<std::size_t>(read_bytes);
            if (!WriteAll(output_fd, buffer.data(), size, &failure)) break;
            if (EVP_DigestUpdate(part_context.get(), buffer.data(), size) != 1 ||
                EVP_DigestUpdate(whole_context.get(), buffer.data(), size) != 1) {
                failure = "update SHA-256 failed while composing final object";
                break;
            }
            part_bytes += static_cast<std::uint64_t>(size);
            total_bytes += static_cast<std::uint64_t>(size);
        }
        (void)::close(input_fd);
        if (!failure.empty()) break;

        unsigned char part_digest[EVP_MAX_MD_SIZE]{};
        unsigned int part_digest_size = 0;
        if (EVP_DigestFinal_ex(
                part_context.get(), part_digest, &part_digest_size
            ) != 1 || part_digest_size == 0) {
            failure = "finalize chunk SHA-256 failed for " + descriptor.storage_part_key;
            break;
        }
        const std::string actual_part_sha256 = HexDigest(part_digest, part_digest_size);
        if (part_bytes != descriptor.expected_size ||
            actual_part_sha256 != descriptor.expected_sha256) {
            failure = "stored chunk size/checksum mismatch for " + descriptor.storage_part_key;
            break;
        }
    }

    unsigned char whole_digest[EVP_MAX_MD_SIZE]{};
    unsigned int whole_digest_size = 0;
    std::string actual_whole_sha256;
    if (failure.empty()) {
        if (EVP_DigestFinal_ex(
                whole_context.get(), whole_digest, &whole_digest_size
            ) != 1 || whole_digest_size == 0) {
            failure = "finalize whole-file SHA-256 failed";
        } else {
            actual_whole_sha256 = HexDigest(whole_digest, whole_digest_size);
        }
    }

    if (failure.empty() && total_bytes != request.expected_total_size) {
        failure = "composed byte count does not match expected total size";
    }

    if (!failure.empty()) {
        (void)::close(output_fd);
        RemoveNoThrow(temporary);
        return ComposeIoFailure(std::move(failure));
    }

    if (actual_whole_sha256 != request.expected_sha256) {
        (void)::close(output_fd);
        RemoveNoThrow(temporary);
        ComposeObjectResult result;
        result.status = FileStorageStatus::kChecksumMismatch;
        result.bytes_written = total_bytes;
        result.verified_sha256 = actual_whole_sha256;
        result.message = "whole-file SHA-256 does not match BeginUpload expectation";
        return result;
    }

    if (::fsync(output_fd) != 0) {
        const std::string error = std::string("fsync final object failed: ") +
            std::strerror(errno);
        (void)::close(output_fd);
        RemoveNoThrow(temporary);
        return ComposeIoFailure(error);
    }
    if (::close(output_fd) != 0) {
        const std::string error = std::string("close final object failed: ") +
            std::strerror(errno);
        RemoveNoThrow(temporary);
        return ComposeIoFailure(error);
    }

    if (::rename(temporary.c_str(), destination.c_str()) != 0) {
        const std::string error = std::string("atomic final-object rename failed: ") +
            std::strerror(errno);
        RemoveNoThrow(temporary);
        return ComposeIoFailure(error);
    }
    FsyncDirectoryNoThrow(destination.parent_path());

    ComposeObjectResult result;
    result.status = FileStorageStatus::kSucceeded;
    result.bytes_written = total_bytes;
    result.verified_sha256 = actual_whole_sha256;
    result.message = "final object assembled, verified, and atomically published";
    return result;
}

ReadObjectRangeResult LocalFilesystemStorage::ReadObjectRange(
    const ReadObjectRangeRequest& request
) {
    if (request.length == 0 || request.expected_total_size == 0 ||
        request.offset >= request.expected_total_size) {
        return RangeFailure(
            FileStorageStatus::kInvalidArgument,
            "invalid immutable object range"
        );
    }

    std::filesystem::path source;
    if (!ResolveSafePath(request.storage_key, &source)) {
        return RangeFailure(FileStorageStatus::kInvalidArgument, "storage_key is unsafe");
    }

    const int fd = ::open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        if (errno == ENOENT) {
            return RangeFailure(
                FileStorageStatus::kNotFound,
                "AVAILABLE final object is missing"
            );
        }
        return RangeFailure(
            FileStorageStatus::kIoError,
            std::string("open final object failed: ") + std::strerror(errno)
        );
    }

    struct stat metadata {};
    if (::fstat(fd, &metadata) != 0) {
        const std::string error = std::string("fstat final object failed: ") +
            std::strerror(errno);
        (void)::close(fd);
        return RangeFailure(FileStorageStatus::kIoError, error);
    }
    if (!S_ISREG(metadata.st_mode) || metadata.st_size < 0 ||
        static_cast<std::uint64_t>(metadata.st_size) != request.expected_total_size) {
        (void)::close(fd);
        return RangeFailure(
            FileStorageStatus::kDataLoss,
            "AVAILABLE final object type/size disagrees with durable metadata"
        );
    }

    const std::uint64_t available = request.expected_total_size - request.offset;
    const std::uint64_t wanted = std::min(request.length, available);
    const auto max_off_t = static_cast<std::uint64_t>(std::numeric_limits<off_t>::max());
    if (request.offset > max_off_t || wanted - 1 > max_off_t - request.offset) {
        (void)::close(fd);
        return RangeFailure(
            FileStorageStatus::kInvalidArgument,
            "range offset cannot be represented by this filesystem backend"
        );
    }
    if (wanted > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        (void)::close(fd);
        return RangeFailure(FileStorageStatus::kInvalidArgument, "range length is too large");
    }

    std::string data(static_cast<std::size_t>(wanted), '\0');
    std::size_t completed = 0;
    while (completed < data.size()) {
        const ssize_t rc = ::pread(
            fd,
            data.data() + completed,
            data.size() - completed,
            static_cast<off_t>(request.offset + completed)
        );
        if (rc < 0) {
            if (errno == EINTR) continue;
            const std::string error = std::string("pread final object failed: ") +
                std::strerror(errno);
            (void)::close(fd);
            return RangeFailure(FileStorageStatus::kIoError, error);
        }
        if (rc == 0) {
            (void)::close(fd);
            return RangeFailure(
                FileStorageStatus::kDataLoss,
                "AVAILABLE final object ended before durable total_size"
            );
        }
        completed += static_cast<std::size_t>(rc);
    }
    (void)::close(fd);

    const std::string range_sha256 = Sha256Hex(data);
    if (range_sha256.empty()) {
        return RangeFailure(FileStorageStatus::kIoError, "compute range SHA-256 failed");
    }

    ReadObjectRangeResult result;
    result.status = FileStorageStatus::kSucceeded;
    result.offset = request.offset;
    result.data = std::move(data);
    result.range_sha256 = range_sha256;
    result.eof = request.offset + wanted == request.expected_total_size;
    result.message = result.eof ? "final object EOF range read" : "final object range read";
    return result;
}

}  // namespace tinyimx::file
