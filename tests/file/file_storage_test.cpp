#include "services/file/storage/LocalFilesystemStorage.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

bool Expect(bool condition, const char* name) {
    if (condition) {
        std::cout << "[PASS] " << name << '\n';
        return true;
    }
    std::cerr << "[FAIL] " << name << '\n';
    return false;
}

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("tinyimx-m18b1-storage-" + std::to_string(
            static_cast<unsigned long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()))));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);

    tinyimx::file::LocalFilesystemStorage storage(root);
    tinyimx::file::StoreChunkRequest request;
    request.storage_part_key = "uploads/77/chunks/0.part";
    request.data = "hello";
    request.expected_sha256 =
        "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824";

    bool ok = true;
    const auto first = storage.StoreChunkAtomically(request);
    ok = Expect(first.Succeeded() && first.bytes_written == 5,
                "LocalFilesystemStorage.atomic-write") && ok;
    const auto destination = root / "uploads/77/chunks/0.part";
    ok = Expect(ReadAll(destination) == "hello",
                "LocalFilesystemStorage.exact-bytes") && ok;

    const auto retry = storage.StoreChunkAtomically(request);
    ok = Expect(retry.Succeeded() && ReadAll(destination) == "hello" &&
                std::filesystem::file_size(destination) == 5,
                "LocalFilesystemStorage.retry-does-not-append") && ok;

    std::vector<std::thread> threads;
    std::vector<int> results(4, 0);
    for (std::size_t i = 0; i < results.size(); ++i) {
        threads.emplace_back([&, i] { results[i] = storage.StoreChunkAtomically(request).Succeeded(); });
    }
    for (auto& thread : threads) thread.join();
    bool concurrent_ok = true;
    for (int value : results) concurrent_ok = concurrent_ok && (value != 0);
    ok = Expect(concurrent_ok && ReadAll(destination) == "hello" &&
                std::filesystem::file_size(destination) == 5,
                "LocalFilesystemStorage.concurrent-identical-write") && ok;

    request.storage_part_key = "../escape.part";
    ok = Expect(storage.StoreChunkAtomically(request).status ==
                    tinyimx::file::FileStorageStatus::kInvalidArgument,
                "LocalFilesystemStorage.path-traversal-rejected") && ok;

    request.storage_part_key = "uploads/77/chunks/1.part";
    request.expected_sha256 = std::string(64, 'a');
    ok = Expect(storage.StoreChunkAtomically(request).status ==
                    tinyimx::file::FileStorageStatus::kInvalidArgument,
                "LocalFilesystemStorage.checksum-mismatch-rejected") && ok;

    std::filesystem::remove_all(root, ec);
    return ok ? 0 : 1;
}
