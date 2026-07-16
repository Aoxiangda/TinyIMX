#include "common/concurrency/ThreadPoolTypes.h"

namespace tinyimx {
    std::string ThreadPoolStateToString(ThreadPoolState state) {
        switch (state) {
            case ThreadPoolState::kCreated:
                return "Created";
            case ThreadPoolState::kRunning:
                return "Running";
            case ThreadPoolState::kPaused:
                return "Paused";
            case ThreadPoolState::kShuttingDown:
                return "ShuttingDown";
            case ThreadPoolState::kForceStopping:
                return "ForceStopping";
            case ThreadPoolState::kStopped:
                return "Stopped";
            default:
                return "Unknown";
        }
    }

} // namespace tinyimx