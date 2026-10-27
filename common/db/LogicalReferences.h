#pragma once

#include "common/db/MySqlConnection.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace tinyimx::db {

// Parent identities are immutable and retained. The service lifecycle changes
// status (account disable, group disband, upload cancel); it never deletes a
// parent or rewrites its primary key. Administrative physical purge requires a
// separate, quiesced retention workflow. This is the application contract that
// makes existence checks safe without database FKs or per-message row locks.
struct LogicalReference {
    enum class Kind { User, Group, GroupMessage, File, Upload, NativeOwner };
    Kind kind;
    std::uint64_t id;
    std::uint64_t owner{0};

    static LogicalReference User(std::uint64_t id) { return {Kind::User, id}; }
    static LogicalReference Group(std::uint64_t id) { return {Kind::Group, id}; }
    static LogicalReference GroupMessage(std::uint64_t id, std::uint64_t group) {
        return {Kind::GroupMessage, id, group};
    }
    static LogicalReference File(std::uint64_t id, std::uint64_t user) {
        return {Kind::File, id, user};
    }
    static LogicalReference Upload(std::uint64_t id, std::uint64_t file) {
        return {Kind::Upload, id, file};
    }
    static LogicalReference NativeOwner() { return {Kind::NativeOwner, 1}; }
};

enum class LogicalReferenceStatus { Exists, Missing, Invalid, Unavailable };

// All identifiers originate in the enum; only unsigned numeric values enter SQL.
// Check the entire batch before the first mutation. A caller transaction still
// owns message/outbox atomicity; this helper never begins, commits or retries it.
inline LogicalReferenceStatus CheckReferences(MySqlConnection* connection,
                                              std::vector<LogicalReference> references) {
    if (!connection || !connection->IsConnected()) return LogicalReferenceStatus::Unavailable;
    if (references.empty() || references.size() > 8192) return LogicalReferenceStatus::Invalid;
    using Kind = LogicalReference::Kind;
    for (const auto& ref : references) {
        if (!ref.id || ((ref.kind == Kind::GroupMessage || ref.kind == Kind::File ||
                         ref.kind == Kind::Upload) && !ref.owner)) return LogicalReferenceStatus::Invalid;
    }
    const auto key = [](const auto& r) { return std::tuple(r.kind, r.id, r.owner); };
    std::sort(references.begin(), references.end(), [&](const auto& a, const auto& b) {
        return key(a) < key(b);
    });
    references.erase(std::unique(references.begin(), references.end(),
        [&](const auto& a, const auto& b) { return key(a) == key(b); }), references.end());
    for (std::size_t begin = 0; begin < references.size(); begin += 256) {
        std::string sql = "SELECT ";
        const auto end = std::min(begin + 256, references.size());
        for (std::size_t i = begin; i < end; ++i) {
            if (i != begin) sql += " AND ";
            const auto& ref = references[i];
            sql += "EXISTS(SELECT 1 FROM ";
            switch (ref.kind) {
                case Kind::User: sql += "im_users WHERE user_id="; break;
                case Kind::Group: sql += "im_groups WHERE group_id="; break;
                case Kind::GroupMessage: sql += "im_group_messages WHERE message_id="; break;
                case Kind::File: sql += "im_files WHERE file_id="; break;
                case Kind::Upload: sql += "im_file_upload_sessions WHERE upload_id="; break;
                case Kind::NativeOwner: sql += "im_native_private_owner WHERE owner_slot="; break;
                default: return LogicalReferenceStatus::Invalid;
            }
            sql += std::to_string(ref.id);
            if (ref.kind == Kind::GroupMessage) sql += " AND group_id=" + std::to_string(ref.owner);
            if (ref.kind == Kind::File) sql += " AND owner_user_id=" + std::to_string(ref.owner);
            if (ref.kind == Kind::Upload) sql += " AND file_id=" + std::to_string(ref.owner);
            sql += ')';
        }
        MySqlQueryResult result;
        if (!connection->Query(sql, &result)) return LogicalReferenceStatus::Unavailable;
        if (result.rows == std::vector<std::vector<std::string>>{{"0"}})
            return LogicalReferenceStatus::Missing;
        if (result.rows != std::vector<std::vector<std::string>>{{"1"}})
            return LogicalReferenceStatus::Unavailable;
    }
    return LogicalReferenceStatus::Exists;
}

// Preserve the original fail-closed boolean interface for all other callers.
inline bool ReferencesExist(MySqlConnection* connection,
                            std::vector<LogicalReference> references) {
    return CheckReferences(connection, std::move(references)) == LogicalReferenceStatus::Exists;
}

inline bool ReferencesExist(MySqlConnection* connection,
                            std::initializer_list<LogicalReference> references) {
    return ReferencesExist(connection, std::vector<LogicalReference>(references));
}
}  // namespace tinyimx::db
