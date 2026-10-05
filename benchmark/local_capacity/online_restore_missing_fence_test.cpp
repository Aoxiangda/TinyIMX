#include "services/cache/OnlineStatusCache.h"
#include "gateway/PresenceOrderingKey.h"
#include "gateway/business/BusinessExecutor.h"
#include "common/logging/Logger.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace missing_fence_test {
using Json = nlohmann::json;
using Cache = tinyimx::OnlineStatusCache;
using Status = tinyimx::SetOnlineStatus;
using Executor = tinyimx::BusinessExecutor;
namespace fs = std::filesystem;
using namespace std::chrono_literals;
Json checks = Json::array();
fs::path output;
void save(const std::string& name, const Json& data) {
    auto tmp = output / (name + ".tmp");
    { std::ofstream file(tmp); file << data.dump(2) << '\n'; file.flush();
      if (!file) throw std::runtime_error("OwnEvidenceWrite"); }
    fs::rename(tmp, output / name);
}
void check(const std::string& name, bool pass, Json details = Json::object()) {
    checks.push_back({{"name", name}, {"pass", pass}, {"details", details}});
    save("checks.json", checks);
    if (!pass) throw std::runtime_error("OwnCheck:" + name);
}
std::string key(const std::string& prefix, std::uint64_t id) {
    return prefix + std::to_string(id);
}
void absent(tinyimx::RedisConnectionPool& pool, const std::string& k) {
    auto c = pool.Acquire(); if (!c) throw std::runtime_error("OwnPreflightLease");
    auto value = c->EvalInteger("return redis.call('EXISTS',KEYS[1])", {k}, {});
    if (!value || *value != 0) throw std::runtime_error("OwnFixtureCollisionPreserved");
}
bool owner(Cache& cache, std::uint64_t id, const std::string& gateway,
           const std::string& connection) {
    const auto result = cache.GetOnlineStatus(id);
    return result.Found() && result.record->gateway_id == gateway &&
           result.record->connection_name == connection && result.record->user_id == id;
}
Json snapshot(tinyimx::RedisConnectionPool& pool, const std::string& k) {
    auto c = pool.Acquire(); if (!c) throw std::runtime_error("OwnSnapshotLease");
    auto values = c->EvalStringArray(
        "local t=redis.call('TYPE',KEYS[1]).ok; local v=''; "
        "if t=='string' then v=redis.call('GET',KEYS[1]) "
        "elseif t=='list' then v=cjson.encode(redis.call('LRANGE',KEYS[1],0,-1)) end; "
        "return {t,v,tostring(redis.call('PTTL',KEYS[1]))}", {k}, {});
    if (!values || values->size() != 3) throw std::runtime_error("OwnSnapshotReply");
    return {{"type", (*values)[0]}, {"bytes", (*values)[1]},
            {"pttl", std::stoll((*values)[2])}};
}
void preserved(const std::string& name, const Json& a, const Json& b) {
    check(name + ":bytes-and-type", a["type"] == b["type"] && a["bytes"] == b["bytes"]);
    const auto before = a["pttl"].get<std::int64_t>();
    const auto after = b["pttl"].get<std::int64_t>();
    check(name + ":TTL-preserved", before > 280000 && after <= before &&
          after >= before - 2000, {{"before_ms", before}, {"after_ms", after}});
}
void cache_cases(tinyimx::RedisConnectionPool& pool, const std::string& prefix) {
    Cache cache(&pool, prefix);
    const std::uint64_t first = 61000;
    for (auto id = first; id < first + 20; ++id) absent(pool, key(prefix, id));
    auto stored = cache.SetOnlineIfMissing(first, "local", "old", 120);
    check("missing:stored", stored.Succeeded() && stored.status == Status::kStored);
    check("missing:owner", owner(cache, first, "local", "old"));
    auto bytes = snapshot(pool, key(prefix, first));
    check("missing:TTL", bytes["pttl"].get<int>() > 110000 && bytes["pttl"].get<int>() <= 120000);
    auto repeated = cache.SetOnlineIfMissing(first, "other", "new", 300);
    check("existing:typed-already-exists", repeated.status == Status::kAlreadyExists && !repeated.Succeeded());
    auto after = snapshot(pool, key(prefix, first));
    check("existing:own-record-and-TTL-preserved", bytes["bytes"] == after["bytes"] &&
          after["pttl"].get<int>() <= bytes["pttl"].get<int>() && after["pttl"].get<int>() > 110000);
    check("status:numerics-preserved", static_cast<int>(Status::kStored) == 0 &&
          static_cast<int>(Status::kInvalidArgument) == 1 && static_cast<int>(Status::kRedisError) == 2 &&
          static_cast<int>(Status::kAlreadyExists) == 3);
    check("status:string", tinyimx::SetOnlineStatusToString(Status::kAlreadyExists) == "already_exists");
    for (int kind = 0; kind < 5; ++kind) {
        const auto id = first + 1 + kind; const auto k = key(prefix, id);
        const std::string label = "existing-fixture-" + std::to_string(kind);
        if (kind < 2) {
            check(label + ":fixture", cache.SetOnline(id, kind == 0 ? "remote" : "local",
                  kind == 0 ? "new" : "old", 300).Succeeded());
        } else {
            auto c = pool.Acquire(); if (!c) throw std::runtime_error("OwnFixtureLease");
            if (kind == 4) {
                auto made = c->EvalInteger("redis.call('RPUSH',KEYS[1],'own-list'); return redis.call('EXPIRE',KEYS[1],300)", {k}, {});
                if (!made || *made != 1) throw std::runtime_error("OwnListFixture");
            } else if (!c->SetEx(k, kind == 2 ? "{bad-json" : "17", 300))
                throw std::runtime_error("OwnStringFixture");
        }
        const auto before = snapshot(pool, k);
        check(label + ":not-overwritten", cache.SetOnlineIfMissing(id, "local", "old", 120).status == Status::kAlreadyExists);
        preserved(label, before, snapshot(pool, k));
    }
    Cache null_cache(nullptr, prefix);
    check("null:valid-RedisError", null_cache.SetOnlineIfMissing(first + 8, "g", "c", 120).status == Status::kRedisError);
    for (int kind = 0; kind < 5; ++kind) {
        auto invalid = null_cache.SetOnlineIfMissing(kind == 0 ? 0 : first + 8,
            kind == 1 ? "" : "g", kind == 2 ? "" : "c", kind == 3 ? 0 : kind == 4 ? -1 : 120);
        check("invalid-before-I/O-" + std::to_string(kind), invalid.status == Status::kInvalidArgument);
    }
    tinyimx::RedisConnectionPool uninitialized; Cache uninit(&uninitialized, prefix);
    check("uninitialized:RedisError", uninit.SetOnlineIfMissing(first + 8, "g", "c", 120).status == Status::kRedisError);
    // Actual missing lookup -> remote owner installed -> old missing-only restore.
    const auto race = first + 6;
    check("remote-window:lookup-missing", cache.RefreshOnlineIfMatch(race, "local", "old", 120).status == tinyimx::RefreshOnlineIfMatchStatus::kNotFound);
    check("remote-window:new-owner-set", cache.SetOnline(race, "remote", "new", 300).Succeeded());
    const auto before = snapshot(pool, key(prefix, race));
    check("remote-window:old-restore-AlreadyExists", cache.SetOnlineIfMissing(race, "local", "old", 120).status == Status::kAlreadyExists);
    check("remote-window:owner-preserved", owner(cache, race, "remote", "new"));
    preserved("remote-window", before, snapshot(pool, key(prefix, race)));
    const auto expiry = first + 7;
    check("expiry:fixture", cache.SetOnlineIfMissing(expiry, "local", "expired", 1).Succeeded());
    std::this_thread::sleep_for(1150ms);
    check("expiry:recreated-only-after-expiry", cache.SetOnlineIfMissing(expiry, "local", "fresh", 120).Succeeded() && owner(cache, expiry, "local", "fresh"));
    const auto contest = first + 9; std::atomic<int> winners{0}, exists{0}, errors{0}, winner{-1};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) threads.emplace_back([&, i] {
        try { const auto result = cache.SetOnlineIfMissing(contest, "contest", std::to_string(i), 120);
            if (result.Succeeded()) { ++winners; winner = i; }
            else if (result.status == Status::kAlreadyExists) ++exists; else ++errors;
        } catch (...) { ++errors; }
    });
    for (auto& thread : threads) thread.join();
    check("contest:exactly-one-winner", winners == 1 && exists == 7 && errors == 0,
          {{"stored", winners.load()}, {"already_exists", exists.load()}, {"errors", errors.load()}});
    check("contest:winner-record", owner(cache, contest, "contest", std::to_string(winner.load())));
}
struct Gate {
    std::mutex mutex; std::condition_variable cv; bool active{false}, released{false};
    void enter() { std::unique_lock<std::mutex> lock(mutex); active = true; cv.notify_all();
        if (!cv.wait_for(lock, 5s, [&] { return released; })) throw std::runtime_error("OwnGateTimeout"); }
    bool wait_active() { std::unique_lock<std::mutex> lock(mutex); return cv.wait_for(lock, 3s, [&] { return active; }); }
    void release() { std::lock_guard<std::mutex> lock(mutex); released = true; cv.notify_all(); }
};
struct ReleaseOnExit { std::shared_ptr<Gate> gate; ~ReleaseOnExit() { gate->release(); } };
Executor::TaskSpec task(std::uint64_t id, const std::string& name) {
    Executor::TaskSpec result; result.request.operation = name; result.request.user_id = id;
    result.request.session_epoch = 1; result.request.ordering_key = tinyimx::PresenceUserOrderingKey(id);
    return result;
}
void lifecycle_cases(tinyimx::RedisConnectionPool& pool, const std::string& prefix) {
    Cache cache(&pool, prefix);
    check("ordering:distinct-full-uint64", tinyimx::PresenceUserOrderingKey(0) != tinyimx::PresenceUserOrderingKey(UINT64_MAX) &&
          tinyimx::PresenceUserOrderingKey(1) != tinyimx::PresenceUserOrderingKey(2));
    // The current-session predicate below is explicitly modeled. Work scheduling,
    // cancellation, must-run cleanup and Redis API are actual project code.
    for (int kind = 0; kind < 3; ++kind) {
        const std::uint64_t id = 61100 + kind; absent(pool, key(prefix, id));
        auto gate = std::make_shared<Gate>(); std::atomic<bool> current{true};
        std::atomic<int> restore_status{-1}, cleanup_status{-1}, step{0}, restored_at{0}, cleaned_at{0}, restore_calls{0};
        tinyimx::BusinessExecutorOptions options; options.name = "own-presence-missing-fence";
        options.worker_threads = 4; options.max_pending_tasks = 16; options.stripe_count = 128;
        options.per_stripe_queue_capacity = 8; options.default_deadline = 4000ms; options.shutdown_timeout = 10000ms;
        Executor executor(options); check("lifecycle-" + std::to_string(kind) + ":start", executor.Start());
        ReleaseOnExit release_guard{gate};
        const auto label = "lifecycle-" + std::to_string(kind);
        auto restore = task(id, "own.presence.restore"); restore.still_valid = [&] { return current.load(); };
        restore.work = [&](const Executor::ExecutionContext& context) -> Executor::Completion {
            if (context.CancellationRequested()) return {};
            if (kind == 0) gate->enter(); // Deliberately stop after local-current check.
            ++restore_calls; restore_status = static_cast<int>(cache.SetOnlineIfMissing(id, "local", "old", 120).status);
            restored_at = ++step; return {};
        };
        auto cleanup = task(id, "own.presence.offline");
        cleanup.cancellation_policy = tinyimx::BusinessCancellationPolicy::kMustRun;
        cleanup.work = [&](const Executor::ExecutionContext&) -> Executor::Completion {
            if (kind != 0) gate->enter();
            cleanup_status = static_cast<int>(cache.SetOfflineIfMatch(id, "local", "old").status);
            cleaned_at = ++step; return {};
        };
        if (kind == 0) {
            check(label + ":restore-accepted", executor.Submit(std::move(restore)) == tinyimx::BusinessSubmitStatus::kAccepted);
            check(label + ":restore-active", gate->wait_active()); current = false;
            check(label + ":cleanup-accepted", executor.Submit(std::move(cleanup)) == tinyimx::BusinessSubmitStatus::kAccepted);
        } else {
            current = false;
            check(label + ":cleanup-accepted", executor.Submit(std::move(cleanup)) == tinyimx::BusinessSubmitStatus::kAccepted);
            check(label + ":cleanup-active", gate->wait_active());
            if (kind == 2) { current = true; restore.request.deadline = tinyimx::BusinessClock::now() + 10ms; }
            check(label + ":restore-accepted", executor.Submit(std::move(restore)) == tinyimx::BusinessSubmitStatus::kAccepted);
            if (kind == 2) std::this_thread::sleep_for(30ms);
        }
        gate->release(); check(label + ":safe-drain", executor.ShutdownGraceful());
        check(label + ":offline-final", cache.GetOnlineStatus(id).NotFound());
        if (kind == 0) check(label + ":restore-before-delete", restore_calls == 1 &&
            restore_status == static_cast<int>(Status::kStored) &&
            cleanup_status == static_cast<int>(tinyimx::SetOfflineIfMatchStatus::kDeleted) && restored_at == 1 && cleaned_at == 2);
        else check(label + ":stale-no-I/O", restore_calls == 0 && restore_status == -1 &&
            cleanup_status == static_cast<int>(tinyimx::SetOfflineIfMatchStatus::kNotFound) && cleaned_at == 1);
        const auto s = executor.GetStats();
        const auto terminals = s.completed_total + s.worker_exception_total + s.deadline_expired_before_start_total +
            s.cancelled_before_start_total + s.completion_dropped_total + s.completion_exception_total;
        check(label + ":exact-terminal-accounting", s.accepted_total == 2 && terminals == 2 &&
            s.current_pending_tasks == 0 && s.current_active_tasks == 0 && s.pending_completions == 0 &&
            s.worker_exception_total == 0 && s.completed_total == (kind == 0 ? 2 : 1) &&
            s.cancelled_before_start_total == (kind == 1 ? 1 : 0) &&
            s.deadline_expired_before_start_total == (kind == 2 ? 1 : 0),
            {{"accepted", s.accepted_total}, {"completed", s.completed_total},
             {"cancelled_before_start", s.cancelled_before_start_total},
             {"deadline_before_start", s.deadline_expired_before_start_total}, {"terminals", terminals}});
    }
}
} // namespace missing_fence_test
int main(int argc, char** argv) {
    namespace t = missing_fence_test;
    try {
        if (argc != 5) throw std::runtime_error("OwnArguments"); const std::string mode = argv[1];
        if (mode != "p1" && mode != "p4") throw std::runtime_error("OwnPoolMode");
        t::output = argv[4]; const t::fs::path root = "/home/jackson7/projects/TinyIMX_publish/.local/codex/online-restore-missing-fence-test-20261005";
        if (t::output.parent_path() != root || t::output.filename() != mode || !t::fs::is_directory(t::output) ||
            std::string(argv[3]) != "172.18.0.13") throw std::runtime_error("OwnStageEndpointRejected");
        tinyimx::LoggerConfig logging; logging.level = "warn"; logging.file = "";
        if (!tinyimx::Logger::Instance().Init(logging)) throw std::runtime_error("OwnLoggerInit");
        std::ifstream file(t::fs::path(argv[2]) / "gateway-a.json"); t::Json json;
        if (!file) throw std::runtime_error("OwnConfigFile"); file >> json; json = json.at("redis");
        tinyimx::RedisConfig config; config.enable = json.at("enable").get<bool>(); config.host = argv[3];
        config.port = json.at("port").get<int>(); config.db = json.at("db").get<int>();
        config.password = json.at("password").get<std::string>(); config.pool_size = mode == "p1" ? 1 : 4;
        tinyimx::RedisConnectionPool pool; if (!pool.Initialize(config)) throw std::runtime_error("OwnPoolInitialize");
        const std::string prefix = "codex:online-restore-missing-fence-test-20261005:" + mode + ":";
        t::cache_cases(pool, prefix); t::lifecycle_cases(pool, prefix);
        t::check("pool:all-leases-returned", pool.AvailableCount() == pool.Size());
        tinyimx::OnlineStatusCache cache(&pool, prefix); pool.Shutdown();
        t::check("own-shutdown:RedisError", cache.SetOnlineIfMissing(61999, "g", "c", 120).status == tinyimx::SetOnlineStatus::kRedisError);
        t::save("result.json", {{"status", "ONLINE_RESTORE_MISSING_FENCE_FUNCTIONAL_PASS"},
            {"checks", t::checks.size()}, {"pool_size", config.pool_size}, {"namespace", prefix},
            {"actual_executor_sources", true}, {"live_gateway_sessions_tested", false},
            {"local_current_predicate", "Explicit controlled model, real Executor and cache API"},
            {"production_keys", false}, {"capacity_acceptance", false}});
        return 0;
    } catch (const std::exception& error) {
        if (!t::output.empty() && t::fs::is_directory(t::output)) t::save("failed.json",
            {{"status", "FAIL"}, {"message", error.what()}, {"completed_checks", t::checks.size()}});
        std::cerr << "OWN_MISSING_FENCE_TEST_FAILURE=" << error.what() << '\n'; return 2;
    }
}
