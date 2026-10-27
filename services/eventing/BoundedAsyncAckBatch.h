#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace tinyimx::eventing {

// Count-bounded callback collection, with no detached worker or early success.
// Submit must complete each submitted operation through its callback (the
// transport's existing request timeout still applies). The caller does not
// return or destroy its transport until all submitted callbacks finish.
template <typename Handle, typename Submit>
std::vector<std::error_code> AwaitBoundedAckBatch(
    const std::vector<std::shared_ptr<const Handle>>& handles,
    Submit&& submit
) {
    constexpr std::size_t kMaximumBatch = 256;
    if (handles.size() > kMaximumBatch) {
        throw std::invalid_argument("ACK batch exceeds 256 handles");
    }
    if (handles.empty()) return {};

    struct State {
        explicit State(std::size_t count)
            : errors(count), completed(count, 0), remaining(count) {}
        std::mutex mutex;
        std::condition_variable ready;
        std::vector<std::error_code> errors;
        std::vector<std::uint8_t> completed;
        std::size_t remaining;
    };
    auto state = std::make_shared<State>(handles.size());
    for (std::size_t i = 0; i < handles.size(); ++i) {
        // Retain each opaque transport message through its callback. Duplicate
        // callbacks cannot overwrite the first terminal outcome or underflow.
        auto complete = [state, i, keep_alive = handles[i]](
                            const std::error_code& error) {
            bool finished = false;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->completed[i]) return;
                state->completed[i] = 1;
                state->errors[i] = error;
                finished = --state->remaining == 0;
            }
            if (finished) state->ready.notify_one();
        };
        if (!handles[i]) {
            complete(std::make_error_code(std::errc::invalid_argument));
            continue;
        }
        try {
            // Keep the local callback intact if conversion/submission throws.
            submit(*handles[i], complete);
        } catch (...) {
            complete(std::make_error_code(std::errc::io_error));
        }
    }
    std::unique_lock<std::mutex> lock(state->mutex);
    state->ready.wait(lock, [&] { return state->remaining == 0; });
    return state->errors;
}

}  // namespace tinyimx::eventing
