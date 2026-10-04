#include "common/cache/RedisConnectionPool.h"
#include "common/config/Config.h"
#include "common/logging/Logger.h"
#include "services/cache/UnreadCountCache.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

// Explicit isolated endpoint guard: never run these mutation cases on production Redis.
int main(int argc, char** argv) {
    if (argc != 3 || std::string(argv[2]) != "--owned-isolated-redis") return 2;
    tinyimx::Config config;
    if (!config.LoadFromFile(argv[1]) || !config.Redis().enable ||
        config.Redis().host != "127.0.0.1" || config.Redis().port != 16390 ||
        config.Redis().db != 0 || config.Redis().pool_size != 1) return 2;
    if (!tinyimx::Logger::Instance().Init(config.Logger())) return 2;
    tinyimx::RedisConnectionPool pool;
    if (!pool.Initialize(config.Redis())) return 2;
    const std::string prefix = "tinyimx:codex:unread-snapshot:" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()) + ":";
    tinyimx::UnreadCountCache cache(&pool, prefix);
    unsigned passed = 0, failed = 0;
    const auto check = [&](bool ok, const char* name) {
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << '\n';
        ok ? ++passed : ++failed;
    };
    const auto counts = [](const tinyimx::EnsureUnreadProjectionResult& x,
                           std::int64_t p, std::int64_t t) {
        return x.Succeeded() && x.private_count == p && x.total_count == t;
    };
    const auto private_key = [&](std::uint64_t u, std::uint64_t v) {
        return prefix+"private:"+std::to_string(u)+":"+std::to_string(v);
    };
    const auto total_key = [&](std::uint64_t u) { return prefix+"total:"+std::to_string(u); };
    const auto seed = [&](std::uint64_t u, std::uint64_t v, const std::string& p,
                          const std::string& t) {
        auto c=pool.Acquire();return c && c->Set(private_key(u,v),p) && c->Set(total_key(u),t);
    };
    auto created=cache.EnsurePrivateUnreadProjection(100,1000,1001,true,true);
    check(created.Applied() && created.incremented && counts(created,1,1), "created increments with atomic exact counts");
    auto duplicate=cache.EnsurePrivateUnreadProjection(100,1000,1001,true,true);
    check(!duplicate.Applied() && !duplicate.incremented && counts(duplicate,1,1), "duplicate stable message does not increment");
    auto read=cache.EnsurePrivateUnreadProjection(101,1100,1101,false,true);
    check(read.Applied() && !read.incremented && counts(read,0,0), "read message records marker without increment");
    auto read_retry=cache.EnsurePrivateUnreadProjection(101,1100,1101,true,true);
    check(!read_retry.Applied() && counts(read_retry,0,0), "read retry cannot increment existing marker");
    auto conflict=cache.EnsurePrivateUnreadProjection(100,1200,1201,true,true);
    check(conflict.status==tinyimx::EnsureUnreadProjectionStatus::kIdentityConflict &&
          cache.GetTotalUnread(1200).count==0, "foreign identity cannot mutate counters");
    check(seed(1300,1301,"9007199254740993","9007199254740993"), "seed exact counters above2^53");
    check(counts(cache.EnsurePrivateUnreadProjection(103,1300,1301,true,true),
                 9007199254740994LL,9007199254740994LL), "Lua reply preserves integer precision above2^53");
    check(seed(1400,1401,"9223372036854775806","9223372036854775806"), "seed max-minus-one boundary");
    check(counts(cache.EnsurePrivateUnreadProjection(104,1400,1401,true,true),
                 std::numeric_limits<std::int64_t>::max(),std::numeric_limits<std::int64_t>::max()),
          "increment to int64 max returns exact decimal strings");
    auto overflow=cache.EnsurePrivateUnreadProjection(105,1400,1401,true,true);
    check(overflow.status==tinyimx::EnsureUnreadProjectionStatus::kInvalidValue &&
          cache.GetPrivateUnread(1400,1401).count==std::numeric_limits<std::int64_t>::max(),
          "private overflow is rejected without mutation");
    check(seed(1500,1501,"7","9223372036854775807"), "seed total overflow boundary");
    auto total_overflow=cache.EnsurePrivateUnreadProjection(106,1500,1501,true,true);
    check(total_overflow.status==tinyimx::EnsureUnreadProjectionStatus::kInvalidValue &&
          cache.GetPrivateUnread(1500,1501).count==7, "total overflow does not partially increment private");
    check(seed(1500,1501,"7","7") && counts(cache.EnsurePrivateUnreadProjection(106,1500,1501,true,true),8,8),
          "overflow failure did not install marker; normal retry remains repairable");
    check(seed(1600,1601,"-1","5"), "seed invalid private state");
    check(cache.EnsurePrivateUnreadProjection(107,1600,1601,true,true).status==tinyimx::EnsureUnreadProjectionStatus::kInvalidValue &&
          cache.GetTotalUnread(1600).count==5, "invalid stored private count rejects atomically");
    check(seed(1600,1601,"0","5") && counts(cache.EnsurePrivateUnreadProjection(107,1600,1601,true,true),1,6),
          "invalid-count failure retains repairable marker absence");
    auto legacy=cache.EnsurePrivateUnreadProjection(108,1700,1701,true);
    check(legacy.Applied() && !legacy.private_count && !legacy.total_count &&
          cache.GetPrivateUnread(1700,1701).count==1, "legacy scalar API and consumers retain original behavior");
    check(counts(cache.EnsurePrivateUnreadProjection(108,1700,1701,true,true),1,1),
          "new snapshot recognizes legacy marker without double count");
    check(seed(1700,1701,"0","0") && counts(cache.EnsurePrivateUnreadProjection(108,1700,1701,true,true),0,0),
          "duplicate snapshot returns current counts after concurrent clear");
    check(seed(1700,1701,"invalid","0"), "seed corrupt counter after valid marker");
    auto corrupt_duplicate=cache.EnsurePrivateUnreadProjection(108,1700,1701,true,true);
    check(corrupt_duplicate.Succeeded() && !corrupt_duplicate.private_count && corrupt_duplicate.total_count==0 &&
          cache.GetPrivateUnread(1700,1701).status==tinyimx::GetUnreadCountStatus::kInvalidValue,
          "duplicate status retained; corrupt counter invokes legacy fallback");
    {
        auto c=pool.Acquire();
        check(c && c->SAdd(private_key(1800,1801),"owned-test"), "seed owned wrong-type private key");
    }
    check(cache.EnsurePrivateUnreadProjection(109,1800,1801,true,true).status==tinyimx::EnsureUnreadProjectionStatus::kRedisError,
          "wrong Redis type fails explicitly without success");
    {
        auto c=pool.Acquire();
        check(c && c->EvalInteger("return 7",{},{} )==7, "existing integer EVAL unaffected");
        check(c && c->EvalStringArray("return {'9223372036854775807','0'}",{},{} )==
              std::vector<std::string>{"9223372036854775807","0"}, "strict bulk-string array retains exact text");
        check(c && !c->EvalStringArray("return 7",{},{}), "scalar reply cannot impersonate string array");
        check(c && !c->EvalStringArray("return {'valid',1}",{},{}), "mixed reply types rejected");
        check(c && !c->EvalStringArray("error('owned-error')",{},{}), "Lua execution error retained");
        check(c && c->Ping() && c->LastError().empty(), "healthy connection remains usable after reply/script errors");
        if (c) c->Close();
        check(c && !c->EvalStringArray("return {'0','0','0'}",{},{}),
              "closed connection cannot report successful snapshot");
    }
    {
        auto c=pool.Acquire();
        check(c && c->Ping(), "pool reconnects closed test slot without reinitializing");
    }
    std::atomic<unsigned> failures{0},applied{0};std::vector<std::thread> threads;
    for (unsigned i=0;i<4;++i) threads.emplace_back([&] {
        for (unsigned j=0;j<16;++j) {
            auto x=cache.EnsurePrivateUnreadProjection(110,1900,1901,true,true);
            if (!counts(x,1,1)) ++failures;
            if (x.Applied()) ++applied;
        }
    });
    for (auto& t:threads)t.join();
    check(failures==0 && applied==1 && cache.GetTotalUnread(1900).count==1,
          "four threads pool1:64 retries create one marker and exact one increment");
    check(pool.AvailableCount()==1, "all leased slots returned after success and failure");
    // Preserve owned keys on the dedicated retained Redis; no DEL/FLUSH cleanup.
    std::cout << "RESULT passed=" << passed << " failed=" << failed << " prefix=" << prefix << '\n';
    pool.Shutdown();tinyimx::Logger::Instance().Shutdown();return failed==0 ? 0 : 1;
}
