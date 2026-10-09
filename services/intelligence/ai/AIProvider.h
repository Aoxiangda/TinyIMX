#pragma once

#include "services/intelligence/ai/AITypes.h"

namespace tinyimx::ai {

class IProvider {
public:
    virtual ~IProvider() = default;
    [[nodiscard]] virtual CompletionResult Complete(const CompletionRequest& request) = 0;
};

}  // namespace tinyimx::ai
