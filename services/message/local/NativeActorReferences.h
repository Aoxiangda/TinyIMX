#pragma once
#include "services/message/local/LocalPrivateStore.h"
#include <memory>
#include <string>
namespace tinyimx {class MySqlConnectionPool;}
namespace tinyimx::message::local {
// One local filesystem owner per SQL schema. This is not distributed fencing:
// same-host directory locking remains mandatory; a different machine/path is
// rejected by the durable SQL fingerprint. User IDs are immutable in this mode.
class NativeActorReferences final {
public:
    NativeActorReferences(MySqlConnectionPool*,const LocalPrivateStoreOptions&);
    ~NativeActorReferences();
    NativeActorReferences(const NativeActorReferences&)=delete;
    NativeActorReferences&operator=(const NativeActorReferences&)=delete;
    void ReconcileBeforeServing(LocalPrivateStore&);
    MessageRepositoryMutationResult EnsureActors(std::uint64_t from,std::uint64_t to);
    bool Healthy()const noexcept;
    std::uint64_t OwnerSqlSessionId()const noexcept;
    struct ReferenceSqlStats {
        std::uint64_t guarded_insert_attempts{0};
        std::uint64_t guarded_insert_fast_successes{0};
        std::uint64_t guarded_insert_readbacks{0};
    };
    ReferenceSqlStats SqlStats()const noexcept;
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
}
