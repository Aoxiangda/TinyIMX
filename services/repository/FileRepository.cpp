#include "services/repository/FileRepository.h"

#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <system_error>

namespace tinyimx {
namespace {

bool ParseU64(const std::string& text, std::uint64_t* output) {
    if (output == nullptr || text.empty()) {
        return false;
    }
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) {
        return false;
    }
    *output = value;
    return true;
}

bool ParseU32(const std::string& text, std::uint32_t* output) {
    std::uint64_t value = 0;
    if (!ParseU64(text, &value) || value > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    *output = static_cast<std::uint32_t>(value);
    return true;
}

FileFindResult ParseFileResult(const MySqlQueryResult& query) {
    FileFindResult result;
    result.status = FileRepositoryStatus::kSucceeded;
    if (query.rows.empty()) {
        result.found = false;
        result.message = "file not found";
        return result;
    }
    if (query.rows.size() != 1 || query.rows.front().size() != 16) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "file query returned invalid shape";
        return result;
    }
    const auto& row = query.rows.front();
    auto& file = result.record;
    if (!ParseU64(row[0], &file.file_id) ||
        !ParseU64(row[1], &file.owner_user_id) ||
        !ParseU64(row[4], &file.total_size) ||
        !ParseU32(row[10], &file.status) ||
        !ParseU64(row[11], &file.version)) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "file query contains invalid numeric fields";
        return result;
    }
    file.file_name = row[2];
    file.content_type = row[3];
    file.checksum_algorithm = row[5];
    file.expected_checksum = row[6];
    file.verified_checksum = row[7];
    file.storage_backend = row[8];
    file.storage_key = row[9];
    file.created_at = row[12];
    file.updated_at = row[13];
    file.available_at = row[14];
    file.expires_at = row[15];
    if (file.file_id == 0 || file.owner_user_id == 0 || file.total_size == 0 ||
        file.status < 1 || file.status > 7) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "file query violates durable invariants";
        return result;
    }
    result.found = true;
    result.message = "file found";
    return result;
}

FileUploadBundleFindResult ParseBundleResult(const MySqlQueryResult& query) {
    FileUploadBundleFindResult result;
    result.status = FileRepositoryStatus::kSucceeded;
    if (query.rows.empty()) {
        result.found = false;
        result.message = "file upload bundle not found";
        return result;
    }
    if (query.rows.size() != 1 || query.rows.front().size() != 31) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "file upload bundle query returned invalid shape";
        return result;
    }

    const auto& row = query.rows.front();
    auto& file = result.record.file;
    auto& session = result.record.session;

    if (!ParseU64(row[0], &file.file_id) ||
        !ParseU64(row[1], &file.owner_user_id) ||
        !ParseU64(row[4], &file.total_size) ||
        !ParseU32(row[10], &file.status) ||
        !ParseU64(row[11], &file.version) ||
        !ParseU64(row[16], &session.upload_id) ||
        !ParseU64(row[17], &session.file_id) ||
        !ParseU64(row[18], &session.owner_user_id) ||
        !ParseU64(row[21], &session.total_size) ||
        !ParseU64(row[22], &session.chunk_size) ||
        !ParseU32(row[23], &session.status) ||
        !ParseU64(row[24], &session.version)) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "file upload bundle contains invalid numeric fields";
        return result;
    }

    file.file_name = row[2];
    file.content_type = row[3];
    file.checksum_algorithm = row[5];
    file.expected_checksum = row[6];
    file.verified_checksum = row[7];
    file.storage_backend = row[8];
    file.storage_key = row[9];
    file.created_at = row[12];
    file.updated_at = row[13];
    file.available_at = row[14];
    file.expires_at = row[15];

    session.client_upload_id = row[19];
    session.request_fingerprint = row[20];
    session.created_at = row[25];
    session.updated_at = row[26];
    session.expires_at = row[27];
    session.completed_at = row[28];

    // row[29]/row[30] are duplicated DB-side invariant probes.
    std::uint64_t file_owner_check = 0;
    std::uint64_t file_size_check = 0;
    if (!ParseU64(row[29], &file_owner_check) || !ParseU64(row[30], &file_size_check) ||
        file.file_id == 0 || session.upload_id == 0 ||
        file.file_id != session.file_id ||
        file.owner_user_id != session.owner_user_id ||
        file.owner_user_id != file_owner_check ||
        file.total_size != session.total_size || file.total_size != file_size_check) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "file/upload durable invariants are inconsistent";
        return result;
    }

    result.found = true;
    result.message = "file upload bundle found";
    return result;
}


FileUploadChunkFindResult ParseChunkResult(const MySqlQueryResult& query) {
    FileUploadChunkFindResult result;
    result.status = FileRepositoryStatus::kSucceeded;
    if (query.rows.empty()) {
        result.found = false;
        result.message = "file upload chunk not found";
        return result;
    }
    if (query.rows.size() != 1 || query.rows.front().size() != 14) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "file upload chunk query returned invalid shape";
        return result;
    }
    const auto& row = query.rows.front();
    auto& chunk = result.record;
    if (!ParseU64(row[0], &chunk.upload_id) ||
        !ParseU64(row[1], &chunk.chunk_index) ||
        !ParseU64(row[2], &chunk.file_id) ||
        !ParseU64(row[3], &chunk.owner_user_id) ||
        !ParseU64(row[4], &chunk.byte_offset) ||
        !ParseU64(row[5], &chunk.chunk_size) ||
        !ParseU32(row[9], &chunk.status) ||
        !ParseU64(row[10], &chunk.version)) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "file upload chunk contains invalid numeric fields";
        return result;
    }
    chunk.checksum_algorithm = row[6];
    chunk.checksum = row[7];
    chunk.storage_part_key = row[8];
    chunk.created_at = row[11];
    chunk.updated_at = row[12];
    chunk.stored_at = row[13];
    if (chunk.upload_id == 0 || chunk.file_id == 0 || chunk.owner_user_id == 0 ||
        chunk.chunk_size == 0 || chunk.checksum_algorithm != "sha256" ||
        chunk.checksum.size() != 64 || chunk.storage_part_key.empty() ||
        (chunk.status != 1 && chunk.status != 2)) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "file upload chunk violates durable invariants";
        return result;
    }
    result.found = true;
    result.message = "file upload chunk found";
    return result;
}

std::string BundleSelectPrefix() {
    return
        "SELECT f.file_id, f.owner_user_id, f.file_name, f.content_type, f.total_size, "
        "f.checksum_algorithm, f.expected_checksum, IFNULL(f.verified_checksum, ''), "
        "f.storage_backend, IFNULL(f.storage_key, ''), f.status, f.version, f.created_at, f.updated_at, "
        "IFNULL(f.available_at, ''), IFNULL(f.expires_at, ''), "
        "u.upload_id, u.file_id, u.owner_user_id, u.client_upload_id, u.request_fingerprint, "
        "u.total_size, u.chunk_size, u.status, u.version, u.created_at, u.updated_at, "
        "u.expires_at, IFNULL(u.completed_at, ''), f.owner_user_id, f.total_size "
        "FROM im_file_upload_sessions u JOIN im_files f ON f.file_id = u.file_id ";
}

}  // namespace

FileRepository::FileRepository(MySqlConnectionPool* pool)
    : pool_(pool) {
}

FileUploadBundleFindResult FileRepository::FindByClientUploadId(
    std::uint64_t owner_user_id,
    const std::string& client_upload_id
) {
    FileUploadBundleFindResult result;
    if (pool_ == nullptr || owner_user_id == 0 || client_upload_id.empty()) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid FindByClientUploadId arguments";
        return result;
    }
    auto connection = pool_->Acquire();
    if (!connection) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "FindByClientUploadId failed to acquire connection";
        return result;
    }
    return FindByClientUploadIdOnConnection(
        connection.operator->(), owner_user_id, client_upload_id, false
    );
}

FileUploadBundleFindResult FileRepository::FindUploadBundle(
    std::uint64_t owner_user_id,
    std::uint64_t upload_id
) {
    FileUploadBundleFindResult result;
    if (pool_ == nullptr || owner_user_id == 0 || upload_id == 0) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid FindUploadBundle arguments";
        return result;
    }
    auto connection = pool_->Acquire();
    if (!connection) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "FindUploadBundle failed to acquire connection";
        return result;
    }
    return FindUploadBundleOnConnection(
        connection.operator->(), owner_user_id, upload_id, false
    );
}

FileFindResult FileRepository::FindFileById(
    std::uint64_t owner_user_id,
    std::uint64_t file_id
) {
    FileFindResult result;
    if (pool_ == nullptr || owner_user_id == 0 || file_id == 0) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid FindFileById arguments";
        return result;
    }
    auto connection = pool_->Acquire();
    if (!connection) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "FindFileById failed to acquire connection";
        return result;
    }
    const std::string sql =
        "SELECT file_id, owner_user_id, file_name, content_type, total_size, "
        "checksum_algorithm, expected_checksum, IFNULL(verified_checksum, ''), "
        "storage_backend, IFNULL(storage_key, ''), status, version, created_at, updated_at, "
        "IFNULL(available_at, ''), IFNULL(expires_at, '') FROM im_files WHERE file_id = " +
        std::to_string(file_id) + " AND owner_user_id = " + std::to_string(owner_user_id) +
        " LIMIT 1";
    MySqlQueryResult query;
    if (!connection->Query(sql, &query)) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "FindFileById query failed: " + connection->LastError();
        return result;
    }
    return ParseFileResult(query);
}

FileUploadChunkFindResult FileRepository::FindChunk(
    std::uint64_t owner_user_id,
    std::uint64_t upload_id,
    std::uint64_t chunk_index
) {
    FileUploadChunkFindResult result;
    if (pool_ == nullptr || owner_user_id == 0 || upload_id == 0) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid FindChunk arguments";
        return result;
    }
    auto connection = pool_->Acquire();
    if (!connection) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "FindChunk failed to acquire connection";
        return result;
    }
    return FindChunkOnConnection(
        connection.operator->(), owner_user_id, upload_id, chunk_index, false
    );
}


FileUploadChunkListResult FileRepository::FindChunks(
    std::uint64_t owner_user_id,
    std::uint64_t upload_id
) {
    FileUploadChunkListResult result;
    if (pool_ == nullptr || owner_user_id == 0 || upload_id == 0) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid FindChunks arguments";
        return result;
    }
    auto connection = pool_->Acquire();
    if (!connection) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "FindChunks failed to acquire connection";
        return result;
    }
    return FindChunksOnConnection(connection.operator->(), owner_user_id, upload_id, false);
}

FileBooleanResult FileRepository::UserExistsOnConnection(
    MySqlConnection* connection,
    std::uint64_t user_id
) {
    FileBooleanResult result;
    if (connection == nullptr || user_id == 0) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid UserExistsOnConnection arguments";
        return result;
    }
    MySqlQueryResult query;
    const std::string sql =
        "SELECT user_id FROM im_users WHERE user_id = " + std::to_string(user_id) + " LIMIT 1";
    if (!connection->Query(sql, &query)) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "UserExistsOnConnection query failed: " + connection->LastError();
        return result;
    }
    result.status = FileRepositoryStatus::kSucceeded;
    result.value = !query.rows.empty();
    result.message = result.value ? "user exists" : "user not found";
    return result;
}

FileInsertResult FileRepository::InsertFileOnConnection(
    MySqlConnection* connection,
    std::uint64_t owner_user_id,
    const std::string& file_name,
    const std::string& content_type,
    std::uint64_t total_size,
    const std::string& checksum_algorithm,
    const std::string& expected_checksum
) {
    FileInsertResult result;
    if (connection == nullptr || owner_user_id == 0 || file_name.empty() ||
        content_type.empty() || total_size == 0 || checksum_algorithm != "sha256" ||
        expected_checksum.size() != 64) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid InsertFileOnConnection arguments";
        return result;
    }

    const std::string sql =
        "INSERT INTO im_files (owner_user_id, file_name, content_type, total_size, "
        "checksum_algorithm, expected_checksum, storage_backend, storage_key, status, version) VALUES (" +
        std::to_string(owner_user_id) + ", '" + connection->EscapeString(file_name) + "', '" +
        connection->EscapeString(content_type) + "', " + std::to_string(total_size) + ", 'sha256', '" +
        connection->EscapeString(expected_checksum) + "', 'local_fs', NULL, 1, 1)";
    if (!connection->Execute(sql)) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "insert file failed: " + connection->LastError();
        return result;
    }
    result.file_id = connection->LastInsertId();
    if (result.file_id == 0) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "insert file returned zero file_id";
        return result;
    }
    result.status = FileRepositoryStatus::kSucceeded;
    result.message = "file metadata inserted";
    return result;
}

FileMutationResult FileRepository::SetStorageKeyOnConnection(
    MySqlConnection* connection,
    std::uint64_t file_id,
    const std::string& storage_key
) {
    FileMutationResult result;
    if (connection == nullptr || file_id == 0 || storage_key.empty()) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid SetStorageKeyOnConnection arguments";
        return result;
    }
    const std::string sql =
        "UPDATE im_files SET storage_key = '" + connection->EscapeString(storage_key) +
        "' WHERE file_id = " + std::to_string(file_id) + " AND status = 1";
    if (!connection->Execute(sql)) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "set storage key failed: " + connection->LastError();
        return result;
    }
    result.affected_rows = connection->AffectedRows();
    if (result.affected_rows != 1) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "set storage key affected unexpected row count";
        return result;
    }
    result.status = FileRepositoryStatus::kSucceeded;
    result.message = "storage key set";
    return result;
}

UploadSessionInsertResult FileRepository::InsertUploadSessionIdempotentOnConnection(
    MySqlConnection* connection,
    std::uint64_t file_id,
    std::uint64_t owner_user_id,
    const std::string& client_upload_id,
    const std::string& request_fingerprint,
    std::uint64_t total_size,
    std::uint64_t chunk_size
) {
    UploadSessionInsertResult result;
    if (connection == nullptr || file_id == 0 || owner_user_id == 0 ||
        client_upload_id.empty() || request_fingerprint.size() != 64 ||
        total_size == 0 || chunk_size == 0) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid InsertUploadSessionIdempotentOnConnection arguments";
        return result;
    }

    // ON DUPLICATE KEY blocks behind an uncommitted winner and then exposes the
    // existing upload_id via LAST_INSERT_ID. The caller can rollback its own
    // speculative file row and resolve the durable winner without an orphan.
    const std::string sql =
        "INSERT INTO im_file_upload_sessions (file_id, owner_user_id, client_upload_id, "
        "request_fingerprint, total_size, chunk_size, status, version, expires_at) VALUES (" +
        std::to_string(file_id) + ", " + std::to_string(owner_user_id) + ", '" +
        connection->EscapeString(client_upload_id) + "', '" +
        connection->EscapeString(request_fingerprint) + "', " + std::to_string(total_size) + ", " +
        std::to_string(chunk_size) + ", 1, 1, DATE_ADD(CURRENT_TIMESTAMP(3), INTERVAL 24 HOUR)) "
        "ON DUPLICATE KEY UPDATE upload_id = LAST_INSERT_ID(upload_id)";

    if (!connection->Execute(sql)) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "insert upload session failed: " + connection->LastError();
        return result;
    }
    result.upload_id = connection->LastInsertId();
    result.inserted = connection->AffectedRows() == 1;
    if (result.upload_id == 0) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "insert upload session returned zero upload_id";
        return result;
    }
    result.status = FileRepositoryStatus::kSucceeded;
    result.message = result.inserted ? "upload session inserted" : "upload session already exists";
    return result;
}

FileUploadBundleFindResult FileRepository::FindByClientUploadIdOnConnection(
    MySqlConnection* connection,
    std::uint64_t owner_user_id,
    const std::string& client_upload_id,
    bool for_update
) {
    FileUploadBundleFindResult result;
    if (connection == nullptr || owner_user_id == 0 || client_upload_id.empty()) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid FindByClientUploadIdOnConnection arguments";
        return result;
    }
    std::string sql = BundleSelectPrefix() +
        "WHERE u.owner_user_id = " + std::to_string(owner_user_id) +
        " AND u.client_upload_id = '" + connection->EscapeString(client_upload_id) + "' LIMIT 1";
    if (for_update) {
        sql += " FOR UPDATE";
    }
    MySqlQueryResult query;
    if (!connection->Query(sql, &query)) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "FindByClientUploadIdOnConnection query failed: " + connection->LastError();
        return result;
    }
    return ParseBundleResult(query);
}

FileUploadBundleFindResult FileRepository::FindUploadBundleOnConnection(
    MySqlConnection* connection,
    std::uint64_t owner_user_id,
    std::uint64_t upload_id,
    bool for_update
) {
    FileUploadBundleFindResult result;
    if (connection == nullptr || owner_user_id == 0 || upload_id == 0) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid FindUploadBundleOnConnection arguments";
        return result;
    }
    std::string sql = BundleSelectPrefix() +
        "WHERE u.owner_user_id = " + std::to_string(owner_user_id) +
        " AND u.upload_id = " + std::to_string(upload_id) + " LIMIT 1";
    if (for_update) {
        sql += " FOR UPDATE";
    }
    MySqlQueryResult query;
    if (!connection->Query(sql, &query)) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "FindUploadBundleOnConnection query failed: " + connection->LastError();
        return result;
    }
    return ParseBundleResult(query);
}


FileUploadChunkInsertResult FileRepository::InsertChunkIdempotentOnConnection(
    MySqlConnection* connection,
    std::uint64_t upload_id,
    std::uint64_t chunk_index,
    std::uint64_t file_id,
    std::uint64_t owner_user_id,
    std::uint64_t byte_offset,
    std::uint64_t chunk_size,
    const std::string& checksum_algorithm,
    const std::string& checksum,
    const std::string& storage_part_key
) {
    FileUploadChunkInsertResult result;
    if (connection == nullptr || upload_id == 0 || file_id == 0 || owner_user_id == 0 ||
        chunk_size == 0 || checksum_algorithm != "sha256" || checksum.size() != 64 ||
        storage_part_key.empty()) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid InsertChunkIdempotentOnConnection arguments";
        return result;
    }

    const std::string sql =
        "INSERT INTO im_file_upload_chunks (upload_id, chunk_index, file_id, owner_user_id, "
        "byte_offset, chunk_size, checksum_algorithm, checksum, storage_part_key, status, version) VALUES (" +
        std::to_string(upload_id) + ", " + std::to_string(chunk_index) + ", " +
        std::to_string(file_id) + ", " + std::to_string(owner_user_id) + ", " +
        std::to_string(byte_offset) + ", " + std::to_string(chunk_size) + ", 'sha256', '" +
        connection->EscapeString(checksum) + "', '" + connection->EscapeString(storage_part_key) +
        "', 1, 1) ON DUPLICATE KEY UPDATE upload_id = upload_id";
    if (!connection->Execute(sql)) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "insert upload chunk failed: " + connection->LastError();
        return result;
    }
    result.inserted = connection->AffectedRows() == 1;
    result.status = FileRepositoryStatus::kSucceeded;
    result.message = result.inserted ? "upload chunk reserved" : "upload chunk already reserved";
    return result;
}

FileUploadChunkFindResult FileRepository::FindChunkOnConnection(
    MySqlConnection* connection,
    std::uint64_t owner_user_id,
    std::uint64_t upload_id,
    std::uint64_t chunk_index,
    bool for_update
) {
    FileUploadChunkFindResult result;
    if (connection == nullptr || owner_user_id == 0 || upload_id == 0) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid FindChunkOnConnection arguments";
        return result;
    }
    std::string sql =
        "SELECT upload_id, chunk_index, file_id, owner_user_id, byte_offset, chunk_size, "
        "checksum_algorithm, checksum, storage_part_key, status, version, created_at, updated_at, "
        "IFNULL(stored_at, '') FROM im_file_upload_chunks WHERE owner_user_id = " +
        std::to_string(owner_user_id) + " AND upload_id = " + std::to_string(upload_id) +
        " AND chunk_index = " + std::to_string(chunk_index) + " LIMIT 1";
    if (for_update) sql += " FOR UPDATE";
    MySqlQueryResult query;
    if (!connection->Query(sql, &query)) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "FindChunkOnConnection query failed: " + connection->LastError();
        return result;
    }
    return ParseChunkResult(query);
}


FileUploadChunkListResult FileRepository::FindChunksOnConnection(
    MySqlConnection* connection,
    std::uint64_t owner_user_id,
    std::uint64_t upload_id,
    bool for_update
) {
    FileUploadChunkListResult result;
    if (connection == nullptr || owner_user_id == 0 || upload_id == 0) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid FindChunksOnConnection arguments";
        return result;
    }
    std::string sql =
        "SELECT upload_id, chunk_index, file_id, owner_user_id, byte_offset, chunk_size, "
        "checksum_algorithm, checksum, storage_part_key, status, version, created_at, updated_at, "
        "IFNULL(stored_at, '') FROM im_file_upload_chunks WHERE owner_user_id = " +
        std::to_string(owner_user_id) + " AND upload_id = " + std::to_string(upload_id) +
        " ORDER BY chunk_index";
    if (for_update) sql += " FOR UPDATE";
    MySqlQueryResult query;
    if (!connection->Query(sql, &query)) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "FindChunksOnConnection query failed: " + connection->LastError();
        return result;
    }
    result.status = FileRepositoryStatus::kSucceeded;
    result.records.reserve(query.rows.size());
    for (const auto& row : query.rows) {
        MySqlQueryResult one;
        one.rows.push_back(row);
        auto parsed = ParseChunkResult(one);
        if (!parsed.Succeeded() || !parsed.found) {
            result.status = parsed.status;
            result.records.clear();
            result.message = parsed.message;
            return result;
        }
        result.records.push_back(std::move(parsed.record));
    }
    result.message = "file upload chunks listed";
    return result;
}

FileMutationResult FileRepository::MarkChunkStoredOnConnection(
    MySqlConnection* connection,
    std::uint64_t owner_user_id,
    std::uint64_t upload_id,
    std::uint64_t chunk_index,
    std::uint64_t chunk_size,
    const std::string& checksum
) {
    FileMutationResult result;
    if (connection == nullptr || owner_user_id == 0 || upload_id == 0 ||
        chunk_size == 0 || checksum.size() != 64) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid MarkChunkStoredOnConnection arguments";
        return result;
    }
    const std::string sql =
        "UPDATE im_file_upload_chunks SET status = 2, version = version + 1, "
        "stored_at = COALESCE(stored_at, CURRENT_TIMESTAMP(3)) WHERE owner_user_id = " +
        std::to_string(owner_user_id) + " AND upload_id = " + std::to_string(upload_id) +
        " AND chunk_index = " + std::to_string(chunk_index) + " AND chunk_size = " +
        std::to_string(chunk_size) + " AND checksum = '" + connection->EscapeString(checksum) +
        "' AND status = 1";
    if (!connection->Execute(sql)) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "mark upload chunk stored failed: " + connection->LastError();
        return result;
    }
    result.affected_rows = connection->AffectedRows();
    if (result.affected_rows > 1) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "mark upload chunk stored affected unexpected row count";
        return result;
    }
    result.status = FileRepositoryStatus::kSucceeded;
    result.message = result.affected_rows == 1 ? "upload chunk marked stored" : "upload chunk already stored";
    return result;
}

FileMutationResult FileRepository::BeginFinalizeOnConnection(
    MySqlConnection* connection,
    std::uint64_t owner_user_id,
    std::uint64_t upload_id,
    std::uint64_t file_id
) {
    FileMutationResult result;
    if (connection == nullptr || owner_user_id == 0 || upload_id == 0 || file_id == 0) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid BeginFinalizeOnConnection arguments";
        return result;
    }
    const std::string session_sql =
        "UPDATE im_file_upload_sessions SET status = 2, version = version + 1 "
        "WHERE upload_id = " + std::to_string(upload_id) +
        " AND owner_user_id = " + std::to_string(owner_user_id) + " AND status = 1";
    if (!connection->Execute(session_sql)) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "begin finalize session failed: " + connection->LastError();
        return result;
    }
    if (connection->AffectedRows() != 1) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "begin finalize session affected unexpected row count";
        return result;
    }
    const std::string file_sql =
        "UPDATE im_files SET status = 2, version = version + 1 WHERE file_id = " +
        std::to_string(file_id) + " AND owner_user_id = " + std::to_string(owner_user_id) +
        " AND status = 1";
    if (!connection->Execute(file_sql)) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "begin finalize file failed: " + connection->LastError();
        return result;
    }
    if (connection->AffectedRows() != 1) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "begin finalize file affected unexpected row count";
        return result;
    }
    result.status = FileRepositoryStatus::kSucceeded;
    result.affected_rows = 2;
    result.message = "upload entered FINALIZING/VERIFYING";
    return result;
}

FileMutationResult FileRepository::CompleteFinalizeOnConnection(
    MySqlConnection* connection,
    std::uint64_t owner_user_id,
    std::uint64_t upload_id,
    std::uint64_t file_id,
    const std::string& verified_checksum
) {
    FileMutationResult result;
    if (connection == nullptr || owner_user_id == 0 || upload_id == 0 || file_id == 0 ||
        verified_checksum.size() != 64) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid CompleteFinalizeOnConnection arguments";
        return result;
    }
    const std::string escaped = connection->EscapeString(verified_checksum);
    const std::string file_sql =
        "UPDATE im_files SET status = 3, verified_checksum = '" + escaped +
        "', available_at = COALESCE(available_at, CURRENT_TIMESTAMP(3)), version = version + 1 "
        "WHERE file_id = " + std::to_string(file_id) + " AND owner_user_id = " +
        std::to_string(owner_user_id) + " AND status = 2 AND expected_checksum = '" + escaped + "'";
    if (!connection->Execute(file_sql)) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "complete finalize file failed: " + connection->LastError();
        return result;
    }
    if (connection->AffectedRows() != 1) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "complete finalize file affected unexpected row count";
        return result;
    }
    const std::string session_sql =
        "UPDATE im_file_upload_sessions SET status = 3, completed_at = COALESCE(completed_at, CURRENT_TIMESTAMP(3)), "
        "version = version + 1 WHERE upload_id = " + std::to_string(upload_id) +
        " AND owner_user_id = " + std::to_string(owner_user_id) + " AND status = 2";
    if (!connection->Execute(session_sql)) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "complete finalize session failed: " + connection->LastError();
        return result;
    }
    if (connection->AffectedRows() != 1) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "complete finalize session affected unexpected row count";
        return result;
    }
    result.status = FileRepositoryStatus::kSucceeded;
    result.affected_rows = 2;
    result.message = "upload completed and file marked AVAILABLE";
    return result;
}

FileMutationResult FileRepository::FailFinalizeChecksumOnConnection(
    MySqlConnection* connection,
    std::uint64_t owner_user_id,
    std::uint64_t file_id,
    const std::string& actual_checksum
) {
    FileMutationResult result;
    if (connection == nullptr || owner_user_id == 0 || file_id == 0 ||
        actual_checksum.size() != 64) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid FailFinalizeChecksumOnConnection arguments";
        return result;
    }
    const std::string sql =
        "UPDATE im_files SET status = 4, verified_checksum = '" +
        connection->EscapeString(actual_checksum) + "', version = version + 1 WHERE file_id = " +
        std::to_string(file_id) + " AND owner_user_id = " + std::to_string(owner_user_id) +
        " AND status = 2";
    if (!connection->Execute(sql)) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "mark finalize checksum failure failed: " + connection->LastError();
        return result;
    }
    result.affected_rows = connection->AffectedRows();
    if (result.affected_rows != 1) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "mark finalize checksum failure affected unexpected row count";
        return result;
    }
    result.status = FileRepositoryStatus::kSucceeded;
    result.message = "whole-file checksum mismatch recorded";
    return result;
}

FileMutationResult FileRepository::CancelUploadOnConnection(
    MySqlConnection* connection,
    std::uint64_t owner_user_id,
    std::uint64_t upload_id,
    std::uint64_t file_id
) {
    FileMutationResult result;
    if (connection == nullptr || owner_user_id == 0 || upload_id == 0 || file_id == 0) {
        result.status = FileRepositoryStatus::kInvalidArgument;
        result.message = "invalid CancelUploadOnConnection arguments";
        return result;
    }

    const std::string session_sql =
        "UPDATE im_file_upload_sessions SET status = 4, version = version + 1 "
        "WHERE upload_id = " + std::to_string(upload_id) +
        " AND owner_user_id = " + std::to_string(owner_user_id) + " AND status = 1";
    if (!connection->Execute(session_sql)) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "cancel upload session failed: " + connection->LastError();
        return result;
    }
    if (connection->AffectedRows() != 1) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "cancel upload session affected unexpected row count";
        return result;
    }

    const std::string file_sql =
        "UPDATE im_files SET status = 5, version = version + 1 "
        "WHERE file_id = " + std::to_string(file_id) +
        " AND owner_user_id = " + std::to_string(owner_user_id) + " AND status = 1";
    if (!connection->Execute(file_sql)) {
        result.status = FileRepositoryStatus::kStorageError;
        result.message = "cancel file metadata failed: " + connection->LastError();
        return result;
    }
    if (connection->AffectedRows() != 1) {
        result.status = FileRepositoryStatus::kInvalidRecord;
        result.message = "cancel file metadata affected unexpected row count";
        return result;
    }

    result.status = FileRepositoryStatus::kSucceeded;
    result.affected_rows = 2;
    result.message = "upload and file metadata canceled";
    return result;
}

}  // namespace tinyimx
