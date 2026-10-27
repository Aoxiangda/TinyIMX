#pragma once
#include "services/message/local/LocalPrivateStore.h"
#include <memory>
#include <string>
namespace tinyimx { class MySqlConnectionPool; }
namespace tinyimx::message::local {
// Read-only compatibility oracle. Its fixed version/collation goes into the
// store marker. Unknown future MySQL rules cannot reinterpret existing keys.
// Default mode checks SQL for every request. The optional cache contains only
// positive immutable parent identities (LogicalReferences.h), never account
// status, authentication, relationship ACLs, sessions or negative results.
// Physical parent purge requires quiescence and recreation of this object.
class MySqlLegacyIdentity final {
public:
    explicit MySqlLegacyIdentity(MySqlConnectionPool* pool);
    MySqlLegacyIdentity(MySqlConnectionPool* pool,bool immutable_ascii_cache_enable);
    ~MySqlLegacyIdentity();
    const std::string& Policy() const noexcept { return policy_; }
    LocalIdentityResult Resolve(std::uint64_t from,std::uint64_t to,const std::string& cid);
    struct CacheStats {std::uint64_t hits{0},sql_checks{0};std::size_t positive_users{0};};
    CacheStats ImmutableCacheStats() const;
private:
    class ImmutableCache;
    MySqlConnectionPool* pool_;
    std::string policy_;
    std::unique_ptr<ImmutableCache> cache_;
};
}
