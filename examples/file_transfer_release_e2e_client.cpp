#include "tinyimx/file/v1/file_service.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include <openssl/evp.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

namespace {
constexpr std::uint64_t kChunkSize = 256ULL * 1024ULL;
constexpr std::uint64_t kRangeSize = 128ULL * 1024ULL;

std::string Sha256Hex(const std::string& input) {
    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int size = 0;
    if (EVP_Digest(input.data(), input.size(), digest, &size, EVP_sha256(), nullptr) != 1) return {};
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < size; ++i) out << std::setw(2) << static_cast<unsigned int>(digest[i]);
    return out.str();
}

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool WriteAll(const std::filesystem::path& path, const std::string& data, bool append) {
    std::ofstream output(path, std::ios::binary | (append ? std::ios::app : std::ios::trunc));
    if (!output) return false;
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    return output.good();
}

std::string BuildPayload() {
    std::string payload;
    for (int i = 0; i < 7; ++i) {
        payload.append(static_cast<std::size_t>(kChunkSize), static_cast<char>('A' + i));
    }
    payload += "M18-C2-RELEASE-E2E-TAIL-IMMUTABLE";
    return payload;
}

using State = std::map<std::string, std::string>;

bool SaveState(const std::filesystem::path& path, const State& state) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    for (const auto& [k, v] : state) out << k << '=' << v << '\n';
    return out.good();
}

State LoadState(const std::filesystem::path& path) {
    State state;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        const auto pos = line.find('=');
        if (pos != std::string::npos) state[line.substr(0, pos)] = line.substr(pos + 1);
    }
    return state;
}

std::uint64_t U64(const State& state, const std::string& key) {
    const auto it = state.find(key);
    if (it == state.end()) return 0;
    try { return std::stoull(it->second); } catch (...) { return 0; }
}

std::unique_ptr<tinyimx::file::v1::FileService::Stub> Stub(const std::string& target) {
    auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    return tinyimx::file::v1::FileService::NewStub(channel);
}

bool WaitReady(tinyimx::file::v1::FileService::Stub* stub) {
    tinyimx::file::v1::GetDownloadInfoRequest request;
    request.set_actor_user_id(1);
    request.set_file_id(1);
    for (int i = 0; i < 30; ++i) {
        grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::milliseconds(250));
        tinyimx::file::v1::GetDownloadInfoResponse response;
        const auto status = stub->GetDownloadInfo(&context, request, &response);
        if (status.error_code() != grpc::StatusCode::UNAVAILABLE &&
            status.error_code() != grpc::StatusCode::DEADLINE_EXCEEDED) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

bool GetInfo(
    tinyimx::file::v1::FileService::Stub* stub,
    std::uint64_t actor,
    std::uint64_t file_id,
    tinyimx::file::v1::GetDownloadInfoResponse* response,
    grpc::Status* output_status = nullptr
) {
    tinyimx::file::v1::GetDownloadInfoRequest request;
    request.set_actor_user_id(actor);
    request.set_file_id(file_id);
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
    const auto status = stub->GetDownloadInfo(&context, request, response);
    if (output_status) *output_status = status;
    return status.ok();
}

bool ReadRange(
    tinyimx::file::v1::FileService::Stub* stub,
    std::uint64_t actor,
    std::uint64_t file_id,
    std::uint64_t offset,
    std::uint64_t length,
    const std::string& sha,
    tinyimx::file::v1::ReadFileRangeResponse* response,
    grpc::Status* output_status = nullptr
) {
    tinyimx::file::v1::ReadFileRangeRequest request;
    request.set_actor_user_id(actor);
    request.set_file_id(file_id);
    request.set_offset(offset);
    request.set_length(length);
    request.set_if_match_sha256(sha);
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(3));
    const auto status = stub->ReadFileRange(&context, request, response);
    if (output_status) *output_status = status;
    return status.ok();
}

int Prepare(const std::string& target, const std::filesystem::path& dir, std::uint64_t actor) {
    std::filesystem::create_directories(dir);
    const std::string payload = BuildPayload();
    const std::string whole_sha = Sha256Hex(payload);
    if (whole_sha.size() != 64 || !WriteAll(dir / "expected.bin", payload, false)) return 2;

    auto stub = Stub(target);
    if (!WaitReady(stub.get())) { std::cerr << "[FAIL] FileService not ready\n"; return 3; }

    tinyimx::file::v1::BeginUploadRequest begin;
    begin.set_actor_user_id(actor);
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    begin.set_client_upload_id("m18c2-release-" + std::to_string(stamp));
    begin.set_file_name("m18-c2-release.bin");
    begin.set_content_type("application/octet-stream");
    begin.set_total_size(payload.size());
    begin.set_checksum_algorithm("sha256");
    begin.set_expected_checksum(whole_sha);
    begin.set_preferred_chunk_size(kChunkSize);
    grpc::ClientContext begin_context;
    begin_context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
    tinyimx::file::v1::BeginUploadResponse begin_response;
    const auto begin_status = stub->BeginUpload(&begin_context, begin, &begin_response);
    if (!begin_status.ok() || begin_response.session().upload_id() == 0 || begin_response.file().file_id() == 0) {
        std::cerr << "[FAIL] BeginUpload: " << begin_status.error_message() << '\n'; return 4;
    }
    const auto upload_id = begin_response.session().upload_id();
    const auto file_id = begin_response.file().file_id();

    const std::uint64_t chunks = 1 + ((payload.size() - 1) / kChunkSize);
    for (std::uint64_t i = 0; i < chunks; ++i) {
        const std::uint64_t offset = i * kChunkSize;
        const std::uint64_t count = std::min<std::uint64_t>(kChunkSize, payload.size() - offset);
        const std::string data = payload.substr(static_cast<std::size_t>(offset), static_cast<std::size_t>(count));
        tinyimx::file::v1::UploadChunkRequest request;
        request.set_actor_user_id(actor);
        request.set_upload_id(upload_id);
        request.set_chunk_index(i);
        request.set_byte_offset(offset);
        request.set_data(data);
        request.set_checksum_algorithm("sha256");
        request.set_checksum(Sha256Hex(data));
        grpc::ClientContext context;
        context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(5));
        tinyimx::file::v1::UploadChunkResponse response;
        const auto status = stub->UploadChunk(&context, request, &response);
        if (!status.ok() || (response.result() != tinyimx::file::v1::UPLOAD_CHUNK_RESULT_STORED &&
                             response.result() != tinyimx::file::v1::UPLOAD_CHUNK_RESULT_REUSED)) {
            std::cerr << "[FAIL] UploadChunk index=" << i << " " << status.error_message() << '\n'; return 5;
        }
    }

    tinyimx::file::v1::FinalizeUploadRequest finalize;
    finalize.set_actor_user_id(actor);
    finalize.set_upload_id(upload_id);
    grpc::ClientContext finalize_context;
    finalize_context.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(10));
    tinyimx::file::v1::FinalizeUploadResponse finalize_response;
    const auto finalize_status = stub->FinalizeUpload(&finalize_context, finalize, &finalize_response);
    if (!finalize_status.ok() ||
        (finalize_response.result() != tinyimx::file::v1::FINALIZE_UPLOAD_RESULT_COMPLETED &&
         finalize_response.result() != tinyimx::file::v1::FINALIZE_UPLOAD_RESULT_REUSED) ||
        finalize_response.verified_checksum() != whole_sha) {
        std::cerr << "[FAIL] FinalizeUpload: " << finalize_status.error_message() << '\n'; return 6;
    }

    tinyimx::file::v1::GetDownloadInfoResponse info;
    if (!GetInfo(stub.get(), actor, file_id, &info) || info.info().verified_checksum() != whole_sha ||
        info.info().total_size() != payload.size()) {
        std::cerr << "[FAIL] GetDownloadInfo before interruption\n"; return 7;
    }

    std::uint64_t next = 0;
    std::string partial;
    for (int i = 0; i < 3; ++i) {
        tinyimx::file::v1::ReadFileRangeResponse range;
        if (!ReadRange(stub.get(), actor, file_id, next, kRangeSize, whole_sha, &range) ||
            range.offset() != next || range.data().empty() || range.range_sha256() != Sha256Hex(range.data())) {
            std::cerr << "[FAIL] partial range before restart\n"; return 8;
        }
        partial += range.data();
        next = range.next_offset();
    }
    if (!WriteAll(dir / "download.bin", partial, false)) return 9;

    State state{{"actor",std::to_string(actor)}, {"file_id",std::to_string(file_id)},
                {"upload_id",std::to_string(upload_id)}, {"total_size",std::to_string(payload.size())},
                {"sha256",whole_sha}, {"next_offset",std::to_string(next)}};
    if (!SaveState(dir / "state.env", state)) return 10;
    std::cout << "[PASS] M18-C2 upload->finalize->partial-download before restart\n";
    std::cout << "M18_C2_STATE file_id=" << file_id << " upload_id=" << upload_id
              << " total_size=" << payload.size() << " next_offset=" << next
              << " sha256=" << whole_sha << '\n';
    return 0;
}

int Resume(const std::string& target, const std::filesystem::path& dir) {
    const State state = LoadState(dir / "state.env");
    const auto actor = U64(state,"actor"), file_id = U64(state,"file_id"), total = U64(state,"total_size");
    auto next = U64(state,"next_offset");
    const std::string sha = state.count("sha256") ? state.at("sha256") : "";
    const std::string expected = ReadAll(dir / "expected.bin");
    std::string downloaded = ReadAll(dir / "download.bin");
    if (actor == 0 || file_id == 0 || total == 0 || sha.size() != 64 || expected.size() != total || downloaded.size() != next) return 20;

    auto stub = Stub(target);
    if (!WaitReady(stub.get())) return 21;
    tinyimx::file::v1::GetDownloadInfoResponse info;
    if (!GetInfo(stub.get(), actor, file_id, &info) || info.info().verified_checksum() != sha || info.info().total_size() != total) {
        std::cerr << "[FAIL] resumed GetDownloadInfo did not revalidate immutable object\n"; return 22;
    }

    while (next < total) {
        tinyimx::file::v1::ReadFileRangeResponse range;
        if (!ReadRange(stub.get(), actor, file_id, next, kRangeSize, sha, &range)) return 23;
        const std::string want = expected.substr(static_cast<std::size_t>(next), range.data().size());
        if (range.offset() != next || range.data() != want || range.range_sha256() != Sha256Hex(range.data()) || range.next_offset() <= next) return 24;
        if (!WriteAll(dir / "download.bin", range.data(), true)) return 25;
        next = range.next_offset();
    }
    downloaded = ReadAll(dir / "download.bin");
    if (downloaded != expected || Sha256Hex(downloaded) != sha) return 26;
    auto updated = state;
    updated["next_offset"] = std::to_string(next);
    if (!SaveState(dir / "state.env", updated)) return 27;
    std::cout << "[PASS] M18-C2 service-restart resume reconstructs exact immutable object\n";
    std::cout << "M18_C2_RESUME file_id=" << file_id << " bytes=" << downloaded.size() << " sha256=" << sha << '\n';
    return 0;
}

int ExpectDataLoss(const std::string& target, const std::filesystem::path& dir) {
    const State state = LoadState(dir / "state.env");
    const auto actor = U64(state,"actor"), file_id = U64(state,"file_id");
    const std::string sha = state.count("sha256") ? state.at("sha256") : "";
    auto stub = Stub(target);
    tinyimx::file::v1::GetDownloadInfoResponse info;
    grpc::Status info_status;
    const bool info_ok = GetInfo(stub.get(), actor, file_id, &info, &info_status);
    if (info_ok || info_status.error_code() != grpc::StatusCode::DATA_LOSS) {
        std::cerr << "[FAIL] corruption/missing object not surfaced as DATA_LOSS: " << info_status.error_message() << '\n';
        return 30;
    }
    tinyimx::file::v1::ReadFileRangeResponse range;
    grpc::Status range_status;
    const bool range_ok = ReadRange(stub.get(), actor, file_id, 0, kRangeSize, sha, &range, &range_status);
    if (range_ok || range_status.error_code() != grpc::StatusCode::DATA_LOSS) return 31;
    std::cout << "[PASS] M18-C2 broken AVAILABLE object fenced as DATA_LOSS\n";
    return 0;
}

int VerifyRecovered(const std::string& target, const std::filesystem::path& dir) {
    const State state = LoadState(dir / "state.env");
    const auto actor = U64(state,"actor"), file_id = U64(state,"file_id");
    const std::string sha = state.count("sha256") ? state.at("sha256") : "";
    const std::string expected = ReadAll(dir / "expected.bin");
    auto stub = Stub(target);
    tinyimx::file::v1::GetDownloadInfoResponse info;
    if (!GetInfo(stub.get(), actor, file_id, &info) || info.info().verified_checksum() != sha) return 40;
    tinyimx::file::v1::ReadFileRangeResponse range;
    if (!ReadRange(stub.get(), actor, file_id, 0, kRangeSize, sha, &range) ||
        range.data() != expected.substr(0, range.data().size())) return 41;
    std::cout << "[PASS] M18-C2 restored final object is reverified and readable\n";
    return 0;
}
}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "usage: file_transfer_release_e2e_client <prepare|resume|expect-data-loss|verify-recovered> <target> <state_dir> [actor_user_id]\n";
        return 64;
    }
    const std::string phase = argv[1];
    const std::string target = argv[2];
    const std::filesystem::path dir = argv[3];
    const std::uint64_t actor = argc >= 5 ? std::stoull(argv[4]) : 10001;
    if (phase == "prepare") return Prepare(target, dir, actor);
    if (phase == "resume") return Resume(target, dir);
    if (phase == "expect-data-loss") return ExpectDataLoss(target, dir);
    if (phase == "verify-recovered") return VerifyRecovered(target, dir);
    std::cerr << "unknown phase: " << phase << '\n';
    return 64;
}
