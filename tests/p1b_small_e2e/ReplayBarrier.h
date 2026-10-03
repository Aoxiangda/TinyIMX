#pragma once
// TEST ONLY. Linked only by tinyimx_p1b_replay_fixture, never gateway_demo.
#include "gateway/business/BusinessExecutor.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace tinyimx::p1b_test {
class ReplayBarrier final {
    struct Gate {
        std::mutex mu;
        std::condition_variable cv;
        bool released{false};
        std::atomic<bool> timed_out{false};
        void Release() {
            { std::lock_guard<std::mutex> lock(mu); released = true; }
            cv.notify_all();
        }
        void Wait() {
            std::unique_lock<std::mutex> lock(mu);
            // Safety fuse, NOT a convergence budget. A blown fuse is FAIL.
            if (!cv.wait_for(lock, std::chrono::seconds(120), [&]{return released;})) {
                timed_out.store(true);
                released = true;
                lock.unlock();
                cv.notify_all();
            }
        }
    };
public:
    ReplayBarrier(BusinessExecutor& executor, const BusinessExecutorOptions& options)
        : executor_(executor), options_(options), gate_(std::make_shared<Gate>()) {
        const char* dir = std::getenv("TINYIMX_P1B_TEST_DIR");
        if (!dir || !*dir) return; // Test Gateway A uses no barrier.
        dir_ = dir;
        if (!std::filesystem::is_directory(dir_))
            throw std::runtime_error("P1B test directory is missing");
        const char* mode = std::getenv("TINYIMX_P1B_TEST_MODE");
        const char* receiver = std::getenv("TINYIMX_P1B_TEST_RECEIVER_ID");
        mode_ = mode ? mode : "";
        if ((mode_ != "global" && mode_ != "stripe") || !receiver)
            throw std::runtime_error("P1B test configuration is invalid");
        std::size_t used = 0;
        receiver_ = std::stoull(receiver, &used);
        if (!receiver_ || used != std::string(receiver).size())
            throw std::runtime_error("P1B receiver identity is invalid");
        thread_ = std::thread([this]{ Run(); });
    }
    ReplayBarrier(const ReplayBarrier&) = delete;
    ReplayBarrier& operator=(const ReplayBarrier&) = delete;
    ~ReplayBarrier() { Stop(); }
    void Stop() noexcept {
        stop_.store(true);
        gate_->Release();
        if (thread_.joinable()) thread_.join();
    }
private:
    bool Exists(const char* file) const { return std::filesystem::exists(dir_ / file); }
    void Write(const char* file, const std::string& text) {
        const auto tmp = dir_ / (std::string(file) + ".tmp");
        { std::ofstream out(tmp); out.exceptions(std::ios::failbit | std::ios::badbit); out << text << '\n'; }
        std::filesystem::rename(tmp, dir_ / file);
    }
    static void Tick() { std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    void Run() noexcept {
        try {
            Write("fixture.ready", "test_only=1");
            while (!stop_.load() && !Exists("arm")) Tick();
            if (stop_.load()) return;
            // All normal startup/login replay must leave before injection.
            const auto quiet_deadline = BusinessClock::now() + std::chrono::seconds(15);
            unsigned quiet = 0;
            while (!stop_.load() && quiet < 5) {
                if (BusinessClock::now() >= quiet_deadline)
                    throw std::runtime_error("replay_not_idle_before_arm");
                quiet = executor_.GetStats().current_pending_tasks == 0 ? quiet + 1 : 0;
                Tick();
            }
            if (stop_.load()) return;
            // No production runtime options change. Only finite test work is added.
            const std::size_t requested = mode_ == "global"
                ? options_.max_pending_tasks
                : options_.per_stripe_queue_capacity + 1; // active + queued
            std::size_t accepted = 0;
            for (; accepted < requested; ++accepted) {
                BusinessExecutor::TaskSpec task;
                task.request.operation = "test.p1b.replay_barrier";
                task.request.deadline = BusinessClock::now() + std::chrono::seconds(120);
                if (mode_ == "stripe") task.request.ordering_key = receiver_;
                task.cancellation_policy = BusinessCancellationPolicy::kMustRun;
                task.work = [gate=gate_](const BusinessExecutor::ExecutionContext&) {
                    gate->Wait(); return BusinessExecutor::Completion{};
                };
                const auto status = executor_.Submit(std::move(task));
                if (status != BusinessSubmitStatus::kAccepted) {
                    // Stripe accounting may include its active task. Actual probe below decides.
                    if (mode_ == "stripe" && status == BusinessSubmitStatus::kHotKeyOverloaded) break;
                    throw std::runtime_error(std::string("barrier_fill_") + BusinessSubmitStatusToString(status));
                }
                // Let the first stripe task leave its queue and enter the barrier.
                if (mode_ == "stripe" && accepted == 0) {
                    const auto until = BusinessClock::now() + std::chrono::seconds(3);
                    while (executor_.GetStats().current_active_tasks == 0) {
                        if (BusinessClock::now() >= until) throw std::runtime_error("stripe_worker_not_active");
                        Tick();
                    }
                }
            }
            BusinessExecutor::TaskSpec probe;
            probe.request.operation = "test.p1b.barrier_probe";
            probe.request.ordering_key = receiver_;
            probe.work = [](const BusinessExecutor::ExecutionContext&) {return BusinessExecutor::Completion{};};
            const auto actual = executor_.Submit(std::move(probe));
            const auto expected = mode_ == "global" ? BusinessSubmitStatus::kOverloaded : BusinessSubmitStatus::kHotKeyOverloaded;
            if (actual != expected)
                throw std::runtime_error(std::string("barrier_probe_") + BusinessSubmitStatusToString(actual));
            Write("barrier.held", "mode="+mode_+"\nreceiver="+std::to_string(receiver_)+
                "\naccepted="+std::to_string(accepted)+"\nprobe="+BusinessSubmitStatusToString(actual)+
                "\nworkers="+std::to_string(options_.worker_threads)+
                "\nmax_pending="+std::to_string(options_.max_pending_tasks)+
                "\nstripes="+std::to_string(options_.stripe_count)+
                "\nper_stripe="+std::to_string(options_.per_stripe_queue_capacity));
            while (!stop_.load() && !Exists("release")) {
                if (gate_->timed_out.load()) throw std::runtime_error("barrier_safety_fuse_expired");
                Tick();
            }
            if (stop_.load()) { gate_->Release(); return; }
            // Publish the boundary before unblocking: replay may run immediately.
            Write("barrier.released", "release_observed=1");
            gate_->Release();
        } catch (const std::exception& e) {
            gate_->Release();
            try { Write("fixture.error", e.what()); } catch (...) {}
        } catch (...) {
            gate_->Release();
            try { Write("fixture.error", "unexpected_fixture_exception"); } catch (...) {}
        }
    }
    BusinessExecutor& executor_;
    const BusinessExecutorOptions options_;
    std::shared_ptr<Gate> gate_;
    std::filesystem::path dir_;
    std::string mode_;
    std::uint64_t receiver_{0};
    std::atomic<bool> stop_{false};
    std::thread thread_;
};
} // namespace tinyimx::p1b_test
