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
        ("tinyimx-m18b2-storage-" + std::to_string(
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

    tinyimx::file::StoreChunkRequest second_part;
    second_part.storage_part_key = "uploads/77/chunks/1.part";
    second_part.data = "world";
    second_part.expected_sha256 =
        "486ea46224d1bb4fb680f34f7c9ad96a8f24ec88be73ea8e5a6c65260e9cb8a7";
    ok = Expect(storage.StoreChunkAtomically(second_part).Succeeded(),
                "LocalFilesystemStorage.second-part-write") && ok;

    tinyimx::file::ComposeObjectRequest compose;
    compose.storage_key = "files/7001";
    compose.expected_total_size = 10;
    compose.expected_sha256 =
        "936a185caaa266bb9cbe981e9e05cb78cd732b0b3280eb944412bb6f8f8f07af";
    compose.parts = {
        {"uploads/77/chunks/0.part", 5,
         "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"},
        {"uploads/77/chunks/1.part", 5,
         "486ea46224d1bb4fb680f34f7c9ad96a8f24ec88be73ea8e5a6c65260e9cb8a7"},
    };
    const auto composed = storage.ComposeObjectAtomically(compose);
    const auto final_path = root / "files/7001";
    ok = Expect(composed.Succeeded() && composed.bytes_written == 10 &&
                ReadAll(final_path) == "helloworld",
                "LocalFilesystemStorage.compose-verified-final-object") && ok;

    const auto compose_retry = storage.ComposeObjectAtomically(compose);
    ok = Expect(compose_retry.Succeeded() && ReadAll(final_path) == "helloworld" &&
                std::filesystem::file_size(final_path) == 10,
                "LocalFilesystemStorage.compose-retry-does-not-append") && ok;

    tinyimx::file::VerifyObjectRequest verify_request;
    verify_request.storage_key = "files/7001";
    verify_request.expected_total_size = 10;
    verify_request.expected_sha256 = compose.expected_sha256;
    const auto verified_object = storage.VerifyObject(verify_request);
    ok = Expect(verified_object.Succeeded() && verified_object.bytes_verified == 10 &&
                verified_object.verified_sha256 == compose.expected_sha256,
                "LocalFilesystemStorage.verify-final-object") && ok;

    {
        std::fstream corrupt(final_path, std::ios::in | std::ios::out | std::ios::binary);
        corrupt.seekp(0);
        corrupt.put('H');
    }
    const auto corrupted_object = storage.VerifyObject(verify_request);
    ok = Expect(corrupted_object.status == tinyimx::file::FileStorageStatus::kChecksumMismatch &&
                corrupted_object.verified_sha256 != compose.expected_sha256,
                "LocalFilesystemStorage.same-size-corruption-detected") && ok;

    {
        std::ofstream restore(final_path, std::ios::binary | std::ios::trunc);
        restore << "helloworld";
    }
    ok = Expect(storage.VerifyObject(verify_request).Succeeded(),
                "LocalFilesystemStorage.corruption-recovery-reverified") && ok;

    tinyimx::file::ReadObjectRangeRequest range;
    range.storage_key = "files/7001";
    range.offset = 2;
    range.length = 4;
    range.expected_total_size = 10;
    range.expected_sha256 = compose.expected_sha256;
    const auto middle = storage.ReadObjectRange(range);
    ok = Expect(middle.Succeeded() && middle.offset == 2 && middle.data == "llow" &&
                middle.range_sha256 ==
                    "ecfb725fceebce1ddc2602061742ba6add1a19af8a344d2c91168b118e14e933" &&
                !middle.eof,
                "LocalFilesystemStorage.bounded-range-read") && ok;

    {
        std::fstream mutate(final_path, std::ios::in | std::ios::out | std::ios::binary);
        mutate.seekp(1);
        mutate.put('X');
        mutate.flush();
    }
    range.offset = 0;
    range.length = 4;
    const auto cache_invalidated = storage.ReadObjectRange(range);
    ok = Expect(cache_invalidated.status == tinyimx::file::FileStorageStatus::kChecksumMismatch ||
                cache_invalidated.status == tinyimx::file::FileStorageStatus::kDataLoss,
                "LocalFilesystemStorage.cached-integrity-invalidated-on-mutation") && ok;
    {
        std::ofstream restore(final_path, std::ios::binary | std::ios::trunc);
        restore << "helloworld";
    }
    ok = Expect(storage.ReadObjectRange(range).Succeeded(),
                "LocalFilesystemStorage.range-recovers-after-exact-object-restore") && ok;

    range.offset = 8;
    range.length = 1024;
    const auto tail = storage.ReadObjectRange(range);
    ok = Expect(tail.Succeeded() && tail.data == "ld" && tail.eof &&
                tail.range_sha256 ==
                    "e5a08ffd3d7509c66e79642edbdcd8ed889269a7164c718afca541304188423d",
                "LocalFilesystemStorage.tail-range-clamped-to-eof") && ok;

    range.offset = 10;
    range.length = 1;
    ok = Expect(storage.ReadObjectRange(range).status ==
                    tinyimx::file::FileStorageStatus::kInvalidArgument,
                "LocalFilesystemStorage.offset-at-eof-rejected") && ok;

    range.storage_key = "files/missing-object";
    range.offset = 0;
    range.length = 1;
    ok = Expect(storage.ReadObjectRange(range).status ==
                    tinyimx::file::FileStorageStatus::kNotFound,
                "LocalFilesystemStorage.missing-available-object-detected") && ok;

    std::filesystem::create_directories(root / "files", ec);
    {
        std::ofstream wrong(root / "files/wrong-size", std::ios::binary);
        wrong << "short";
    }
    range.storage_key = "files/wrong-size";
    ok = Expect(storage.ReadObjectRange(range).status ==
                    tinyimx::file::FileStorageStatus::kDataLoss,
                "LocalFilesystemStorage.size-drift-detected") && ok;

    auto bad_whole = compose;
    bad_whole.storage_key = "files/checksum-mismatch";
    bad_whole.expected_sha256 = std::string(64, 'a');
    const auto bad_whole_result = storage.ComposeObjectAtomically(bad_whole);
    ok = Expect(bad_whole_result.status == tinyimx::file::FileStorageStatus::kChecksumMismatch &&
                bad_whole_result.verified_sha256 ==
                    "936a185caaa266bb9cbe981e9e05cb78cd732b0b3280eb944412bb6f8f8f07af" &&
                !std::filesystem::exists(root / "files/checksum-mismatch"),
                "LocalFilesystemStorage.whole-checksum-mismatch-not-published") && ok;

    auto missing_part = compose;
    missing_part.storage_key = "files/missing-part";
    missing_part.parts[1].storage_part_key = "uploads/77/chunks/missing.part";
    const auto missing_result = storage.ComposeObjectAtomically(missing_part);
    ok = Expect(missing_result.status == tinyimx::file::FileStorageStatus::kIoError &&
                !std::filesystem::exists(root / "files/missing-part"),
                "LocalFilesystemStorage.missing-part-not-published") && ok;

    std::filesystem::remove_all(root, ec);
    return ok ? 0 : 1;
}
