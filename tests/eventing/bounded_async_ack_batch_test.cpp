#include "services/eventing/BoundedAsyncAckBatch.h"

#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace std::chrono_literals;
namespace {
struct Handle { int id; };
using Handles = std::vector<std::shared_ptr<const Handle>>;
using Callback = std::function<void(const std::error_code&)>;
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
Handles Make(std::size_t count) {
    Handles result;
    for (std::size_t i = 0; i < count; ++i)
        result.push_back(std::make_shared<const Handle>(Handle{static_cast<int>(i)}));
    return result;
}
void BoundsAndInline() {
    int calls = 0;
    auto submit = [&](const Handle&, Callback done) { ++calls; done({}); };
    Require(tinyimx::eventing::AwaitBoundedAckBatch(Handles{}, submit).empty(), "empty result");
    Require(calls == 0, "empty batch submits nothing");
    auto results = tinyimx::eventing::AwaitBoundedAckBatch(Make(256), submit);
    Require(results.size() == 256 && calls == 256, "exact bound accepted");
    for (auto result : results) Require(!result, "inline success retained");
    bool rejected = false;
    try { tinyimx::eventing::AwaitBoundedAckBatch(Make(257), submit); }
    catch (const std::invalid_argument&) { rejected = true; }
    Require(rejected && calls == 256, "oversize rejects before any submission");
}
void NullExceptionsAndDuplicateCallbacks() {
    auto handles = Make(5); handles[1].reset(); int calls = 0;
    auto results = tinyimx::eventing::AwaitBoundedAckBatch(handles,
        [&](const Handle& value, Callback done) {
            ++calls;
            if (value.id == 2) throw std::runtime_error("submission failed");
            done({});
            done(std::make_error_code(std::errc::connection_reset));
            if (value.id == 3) throw std::runtime_error("after callback");
        });
    Require(calls == 4 && results.size() == 5, "null never dispatched");
    Require(!results[0] && !results[3] && !results[4], "first outcome wins");
    Require(results[1] == std::errc::invalid_argument, "null has exact failed slot");
    Require(results[2] == std::errc::io_error, "submission throw completes failed slot");
}
void OutOfOrderAndLifetime() {
    auto handles = Make(16);
    std::weak_ptr<const Handle> retained = handles[0];
    std::mutex mutex; std::condition_variable submitted;
    std::vector<Callback> callbacks;
    auto future = std::async(std::launch::async, [&] {
        return tinyimx::eventing::AwaitBoundedAckBatch(handles,
            [&](const Handle&, Callback done) {
                std::lock_guard<std::mutex> lock(mutex);
                callbacks.push_back(std::move(done)); submitted.notify_one();
            });
    });
    {
        std::unique_lock<std::mutex> lock(mutex);
        Require(submitted.wait_for(lock, 2s, [&] { return callbacks.size() == 16; }),
                "all sixteen submitted without per-event wait");
    }
    Require(future.wait_for(10ms) == std::future_status::timeout, "no premature batch success");
    for (std::size_t i = 16; i-- > 1;)
        callbacks[i](i == 7 ? std::make_error_code(std::errc::timed_out) : std::error_code{});
    Require(future.wait_for(10ms) == std::future_status::timeout, "last callback still required");
    callbacks[0]({});
    Require(future.wait_for(2s) == std::future_status::ready, "all callbacks release caller");
    auto results = future.get();
    Require(results.size() == 16 && results[7] == std::errc::timed_out, "input/outcome association");
    for (std::size_t i = 0; i < results.size(); ++i)
        if (i != 7) Require(!results[i], "other outcomes remain successful");
    handles.clear();
    Require(!retained.expired(), "callback retains its transport handle");
    callbacks.clear();
    Require(retained.expired(), "completed callback state does not leak handles");
}
}  // namespace

int main() {
    try {
        BoundsAndInline(); NullExceptionsAndDuplicateCallbacks(); OutOfOrderAndLifetime();
        std::cout << "BOUND_PARALLEL_SUBMISSION_EXACT_RESULTS_NO_EARLY_ACK_LIFETIME_PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
