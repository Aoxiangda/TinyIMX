#include "services/file/storage/LocalFilesystemStorage.h"

#include <openssl/evp.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>

namespace tinyimx::file {
namespace {

std::atomic<std::uint64_t> g_temp_sequence{0};

std::string Sha256Hex(std::string_view data) {
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int digest_size = 0;
    if (EVP_Digest(
            data.data(), data.size(), digest, &digest_size,
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

StoreChunkResult Invalid(std::string message) {
    StoreChunkResult result;
    result.status = FileStorageStatus::kInvalidArgument;
    result.message = std::move(message);
    return result;
}

StoreChunkResult IoFailure(std::string message) {
    StoreChunkResult result;
    result.status = FileStorageStatus::kIoError;
    result.message = std::move(message);
    return result;
}

bool WriteAll(int fd, std::string_view data, std::string* error) {
    std::size_t written = 0;
    while (written < data.size()) {
        const ssize_t rc = ::write(
            fd, data.data() + written, data.size() - written
        );
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

void RemoveNoThrow(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

}  // namespace

LocalFilesystemStorage::LocalFilesystemStorage(std::filesystem::path root)
    : root_(std::move(root)) {
    if (!root_.empty()) {
        root_ = root_.lexically_normal();
    }
}

bool LocalFilesystemStorage::ResolveSafePath(
    const std::string& storage_part_key,
    std::filesystem::path* output
) const {
    if (output == nullptr || root_.empty() || storage_part_key.empty()) {
        return false;
    }
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
    if (request.data.empty()) {
        return Invalid("chunk data must not be empty");
    }
    if (request.expected_sha256.size() != 64) {
        return Invalid("expected chunk SHA-256 must contain 64 hex characters");
    }
    const std::string actual_sha256 = Sha256Hex(request.data);
    if (actual_sha256.empty()) {
        return IoFailure("failed to compute chunk SHA-256");
    }
    if (actual_sha256 != request.expected_sha256) {
        return Invalid("chunk payload does not match expected SHA-256");
    }

    std::filesystem::path destination;
    if (!ResolveSafePath(request.storage_part_key, &destination)) {
        return Invalid("storage_part_key is unsafe");
    }

    std::error_code ec;
    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        return IoFailure("create chunk directory failed: " + ec.message());
    }

    const std::uint64_t sequence = g_temp_sequence.fetch_add(1, std::memory_order_relaxed);
    const std::filesystem::path temporary = destination.string() + ".tmp." +
        std::to_string(static_cast<unsigned long long>(::getpid())) + "." +
        std::to_string(sequence);

    const int fd = ::open(
        temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600
    );
    if (fd < 0) {
        return IoFailure(std::string("open temporary chunk failed: ") + std::strerror(errno));
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
        return IoFailure(std::move(write_error));
    }

    if (::rename(temporary.c_str(), destination.c_str()) != 0) {
        const std::string error = std::string("atomic chunk rename failed: ") +
            std::strerror(errno);
        RemoveNoThrow(temporary);
        return IoFailure(error);
    }

    const int parent_fd = ::open(
        destination.parent_path().c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC
    );
    if (parent_fd >= 0) {
        (void)::fsync(parent_fd);
        (void)::close(parent_fd);
    }

    StoreChunkResult result;
    result.status = FileStorageStatus::kSucceeded;
    result.bytes_written = static_cast<std::uint64_t>(request.data.size());
    result.verified_sha256 = actual_sha256;
    result.message = "chunk atomically stored";
    return result;
}

}  // namespace tinyimx::file
