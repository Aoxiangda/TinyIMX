#pragma once

#include "services/intelligence/mcp/McpTypes.h"

#include <memory>
#include <string>
#include <unordered_set>

namespace tinyimx::mcp {

struct AuthResult {
    bool ok{false};
    Principal principal;
    std::string error;
};

class AccessTokenVerifier {
public:
    virtual ~AccessTokenVerifier() = default;
    virtual AuthResult Verify(const std::string& authorization_header) const = 0;
};

class StaticTokenVerifier final : public AccessTokenVerifier {
public:
    StaticTokenVerifier(std::string token, std::uint64_t user_id, std::string subject,
                        std::unordered_set<std::string> scopes);

    AuthResult Verify(const std::string& authorization_header) const override;

private:
    static bool ConstantTimeEqual(const std::string& lhs, const std::string& rhs) noexcept;

    std::string token_;
    Principal principal_;
};

}  // namespace tinyimx::mcp
