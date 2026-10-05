#include "gateway/OnlineStatusMaintenance.h"
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

namespace lifecycle_test {
using Maintenance = tinyimx::OnlineStatusMaintenance;
using Cache = tinyimx::OnlineStatusCache;
using Json = nlohmann::json;
namespace fs = std::filesystem;
using namespace std::chrono_literals;
fs::path output; Json checks = Json::array();
void save(const std::string& name, const Json& data) {
    auto tmp = output / (name + ".tmp");
    { std::ofstream file(tmp); file << data.dump(2) << '\n'; file.flush();
      if (!file) throw std::runtime_error("OwnEvidenceWrite"); }
    fs::rename(tmp, output / name);
}
void check(const std::string& name, bool passed, Json details = Json::object()) {
    checks.push_back({{"name", name}, {"pass", passed}, {"details", details}}); save("checks.json", checks);
    if (!passed) throw std::runtime_error("OwnCheck:" + name);
}
struct Gate {
    std::mutex mutex; std::condition_variable cv; int count{0}; bool released{false};
    void enter() { std::unique_lock lock(mutex); ++count; cv.notify_all();
        if (!cv.wait_for(lock, 8s, [&] { return released; })) throw std::runtime_error("OwnGateTimeout"); }
    bool wait(int target) { std::unique_lock lock(mutex); return cv.wait_for(lock, 3s, [&] { return count >= target; }); }
    void release() { std::lock_guard lock(mutex); released = true; cv.notify_all(); }
};
struct ReleaseOnExit { std::shared_ptr<Gate> gate; ~ReleaseOnExit() { gate->release(); } };
struct JoinStop { std::shared_ptr<Gate> gate; std::thread thread;
    ~JoinStop() { gate->release(); if (thread.joinable()) thread.join(); } };
Maintenance::Job job(std::uint64_t id) {
    Maintenance::Job result; result.request = {id, "lifecycle", "owner-" + std::to_string(id), 120};
    result.still_valid = [] { return true; }; return result;
}
void fixture(tinyimx::RedisConnectionPool& pool, Cache& cache, const std::string& prefix, std::uint64_t id) {
    { auto c = pool.Acquire(); if (!c) throw std::runtime_error("OwnFixtureLease");
      auto exists = c->EvalInteger("return redis.call('EXISTS',KEYS[1])", {prefix + std::to_string(id)}, {});
      if (!exists || *exists) throw std::runtime_error("OwnFixtureCollisionPreserved"); }
    if (!cache.SetOnline(id, "lifecycle", "owner-" + std::to_string(id), 300).Succeeded())
        throw std::runtime_error("OwnFixtureStore");
}
Json stats(const Maintenance::Stats& s) {
    return {{"accepted", s.accepted}, {"terminals", s.Terminals()}, {"completed", s.completed},
        {"deadline_before_io", s.deadline_before_io}, {"cancelled_before_io", s.cancelled_before_io},
        {"cancelled_before_callback", s.cancelled_before_callback}, {"worker_exception", s.worker_exception},
        {"completion_exception", s.completion_exception}, {"pending", s.pending}, {"peak_pending", s.peak_pending},
        {"batch_calls", s.batch_calls}, {"batch_items", s.batch_items}, {"max_batch", s.max_observed_batch}};
}
void capacity(tinyimx::RedisConnectionPool& pool, Cache& cache, const std::string& prefix) {
    for (std::uint64_t id = 62000; id < 62008; ++id) fixture(pool, cache, prefix, id);
    auto gate = std::make_shared<Gate>(); std::atomic<int> calls{0}, callbacks{0}; std::atomic<bool> first_current{true};
    Maintenance::Options options; options.workers = 1; options.max_pending = 8;
    Maintenance maintenance(options, [&](const std::vector<Maintenance::Request>& requests) {
        auto result = cache.RefreshOnlineIfMatchBatch(requests); if (++calls == 1) gate->enter(); return result;
    });
    ReleaseOnExit release_guard{gate};
    check("capacity:start", maintenance.Start()); check("capacity:no-second-start", !maintenance.Start());
    auto first = job(62000); first.still_valid = [&] { return first_current.load(); };
    first.finished = [&](const Maintenance::Result&) { ++callbacks; };
    check("capacity:first-accepted", maintenance.TrySubmit(std::move(first)) == Maintenance::SubmitStatus::kAccepted);
    check("capacity:first-inflight", gate->wait(1));
    for (int i = 1; i < 8; ++i) {
        auto item = job(62000 + i); item.finished = [&](const Maintenance::Result&) { ++callbacks; };
        if (i == 1) item.still_valid = [] { return false; };
        if (i == 2) item.deadline = Maintenance::Clock::now() + 10ms;
        if (i == 3) item.still_valid = []() -> bool { throw std::runtime_error("OwnProbeException"); };
        if (i == 4) item.finished = [](const Maintenance::Result&) { throw std::runtime_error("OwnCallbackException"); };
        check("capacity:queued-" + std::to_string(i), maintenance.TrySubmit(std::move(item)) == Maintenance::SubmitStatus::kAccepted);
    }
    check("capacity:inflight-ready-input-counted", maintenance.GetStats().pending == 8);
    check("capacity:overload-no-wait", maintenance.TrySubmit(job(62000)) == Maintenance::SubmitStatus::kOverloaded);
    auto invalid = job(0); check("capacity:invalid-rejected", maintenance.TrySubmit(std::move(invalid)) == Maintenance::SubmitStatus::kInvalidArgument);
    first_current = false; std::this_thread::sleep_for(30ms);
    JoinStop stop{gate, std::thread([&] { maintenance.Stop(); })};
    const auto deadline = Maintenance::Clock::now() + 3s;
    while (maintenance.IsAccepting() && Maintenance::Clock::now() < deadline) std::this_thread::sleep_for(1ms);
    check("capacity:stop-fences-admission", !maintenance.IsAccepting());
    check("capacity:shutdown-rejected", maintenance.TrySubmit(job(62000)) == Maintenance::SubmitStatus::kShuttingDown);
    gate->release(); stop.thread.join(); maintenance.Stop();
    const auto s = maintenance.GetStats(); save("capacity-stats.json", stats(s));
    check("capacity:exact-terminal-reasons", s.accepted == 8 && s.Terminals() == 8 && s.pending == 0 &&
        s.peak_pending == 8 && s.completed == 3 && s.deadline_before_io == 1 && s.cancelled_before_io == 1 &&
        s.cancelled_before_callback == 1 && s.worker_exception == 1 && s.completion_exception == 1, stats(s));
    check("capacity:callback-isolation", callbacks == 3);
    check("capacity:before-I/O-filtered", s.batch_items == 5 && s.refreshed == 5 && s.max_observed_batch <= 16);
    check("capacity:rejection-counts", s.rejected_overload == 1 && s.rejected_invalid == 1 && s.rejected_shutdown == 1);
    check("capacity:no-restart-after-stop", !maintenance.Start());
}
void batching(tinyimx::RedisConnectionPool& pool, Cache& cache, const std::string& prefix) {
    for (std::uint64_t id = 62500; id < 62536; ++id) fixture(pool, cache, prefix, id);
    auto gate = std::make_shared<Gate>(); std::atomic<int> calls{0}, completed{0}, bad{0};
    Maintenance::Options options; options.workers = 4; options.max_pending = 64;
    // Give evidence writes time to enqueue a complete group behind the four
    // deliberately held workers. This is a controlled grouping test, not a
    // latency measurement; production and the capacity test use 5 ms.
    options.flush_delay = 500ms;
    Maintenance maintenance(options, [&](const std::vector<Maintenance::Request>& requests) {
        const int number = ++calls;
        if (number <= 4) gate->enter();
        return cache.RefreshOnlineIfMatchBatch(requests);
    }); ReleaseOnExit release_guard{gate};
    check("batching:start", maintenance.Start());
    for (int i = 0; i < 36; ++i) {
        auto item = job(62500 + i);
        if (i >= 4 && i % 3 == 0) item.request.connection_name = "stale";
        const bool stale = i >= 4 && i % 3 == 0;
        item.finished = [&, stale](const Maintenance::Result& result) {
            if (result.status != (stale ? tinyimx::RefreshOnlineIfMatchStatus::kMismatch :
                                        tinyimx::RefreshOnlineIfMatchStatus::kRefreshed)) ++bad;
            ++completed;
        };
        check("batching:accepted-" + std::to_string(i), maintenance.TrySubmit(std::move(item)) == Maintenance::SubmitStatus::kAccepted);
        if (i < 4) check("batching:four-fixed-worker-" + std::to_string(i), gate->wait(i + 1));
    }
    check("batching:bounded-at36", maintenance.GetStats().pending == 36);
    gate->release(); maintenance.Stop(); const auto s = maintenance.GetStats(); save("batching-stats.json", stats(s));
    check("batching:exact-results-and-terminals", completed == 36 && bad == 0 && s.accepted == 36 &&
        s.completed == s.Terminals() && s.Terminals() == 36 && s.pending == 0 && s.worker_exception == 0 &&
        s.completion_exception == 0 && s.batch_items == 36 && s.max_observed_batch == 16, stats(s));
    check("batching:actual-lease-batches", s.batch_calls >= 6 && s.batch_calls <= 8 && s.mismatch == 10 && s.refreshed == 26);
    for (std::uint64_t id = 62500; id < 62536; ++id) {
        auto record = cache.GetOnlineStatus(id);
        check("batching:owner-preserved-" + std::to_string(id), record.Found() &&
              record.record->gateway_id == "lifecycle" && record.record->connection_name == "owner-" + std::to_string(id));
    }
}
void failures(Cache& cache) {
    for (std::uint64_t id = 62990; id < 62993; ++id)
        check("worker-faults:missing-fixture-" + std::to_string(id), cache.GetOnlineStatus(id).NotFound());
    Maintenance::Options invalid; invalid.workers = 0;
    Maintenance rejected(invalid, [&](const std::vector<Maintenance::Request>& r) { return cache.RefreshOnlineIfMatchBatch(r); });
    check("invalid-config:Start-fails", !rejected.Start());
    check("before-start:shutdown", rejected.TrySubmit(job(62990)) == Maintenance::SubmitStatus::kShuttingDown);
    Maintenance::Options options; options.workers = 1; options.max_batch = 1; options.max_pending = 3;
    std::atomic<int> calls{0}, callbacks{0};
    Maintenance maintenance(options, [&](const std::vector<Maintenance::Request>& r) {
        const auto number = ++calls; if (number == 1) throw std::runtime_error("OwnBackendException");
        if (number == 2) return std::vector<Maintenance::Result>{};
        return cache.RefreshOnlineIfMatchBatch(r);
    });
    check("worker-faults:start", maintenance.Start());
    for (int i = 0; i < 3; ++i) { auto item = job(62990 + i);
        item.finished = [&](const Maintenance::Result& result) { if (result.status == tinyimx::RefreshOnlineIfMatchStatus::kNotFound) ++callbacks; };
        check("worker-faults:accepted-" + std::to_string(i), maintenance.TrySubmit(std::move(item)) == Maintenance::SubmitStatus::kAccepted); }
    maintenance.Stop(); const auto s = maintenance.GetStats(); save("worker-fault-stats.json", stats(s));
    check("worker-faults:isolated-terminal-accounting", s.accepted == 3 && s.Terminals() == 3 && s.pending == 0 &&
        s.worker_exception == 2 && s.completed == 1 && s.missing == 1 && callbacks == 1, stats(s));
}
} // namespace lifecycle_test
int main(int argc, char** argv) {
    namespace t = lifecycle_test;
    try {
        if (argc != 5) throw std::runtime_error("OwnArguments"); const std::string mode = argv[1];
        if (mode != "p1" && mode != "p4") throw std::runtime_error("OwnPoolMode");
        t::output = argv[4]; const t::fs::path root = "/home/jackson7/projects/TinyIMX_publish/.local/codex/online-maintenance-lifecycle-test-20261005";
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
        const std::string prefix = "codex:online-maintenance-lifecycle-test-20261005:" + mode + ":";
        t::Cache cache(&pool, prefix); t::capacity(pool, cache, prefix); t::batching(pool, cache, prefix); t::failures(cache);
        t::check("pool:all-leases-returned", pool.AvailableCount() == pool.Size());
        t::save("result.json", {{"status", "ONLINE_MAINTENANCE_LIFECYCLE_FUNCTIONAL_PASS"},
            {"checks", t::checks.size()}, {"pool_size", config.pool_size}, {"namespace", prefix},
            {"controlled_actual_cache_and_component", true}, {"live_gateway_sessions_tested", false},
            {"global_faults_or_deletion", false}, {"capacity_acceptance", false}}); return 0;
    } catch (const std::exception& error) {
        if (!t::output.empty() && t::fs::is_directory(t::output)) t::save("failed.json",
            {{"status", "FAIL"}, {"message", error.what()}, {"completed_checks", t::checks.size()}});
        std::cerr << "OWN_MAINTENANCE_LIFECYCLE_FAILURE=" << error.what() << '\n'; return 2;
    }
}
