#include "services/intelligence/mcp/McpAuth.h"

#include <algorithm>

namespace tinyimx::mcp {

StaticTokenVerifier::StaticTokenVerifier(std::string token, std::uint64_t user_id,
                                         std::string subject,
                                         std::unordered_set<std::string> scopes)
    : token_(std::move(token)) {
    principal_.user_id = user_id;
    principal_.subject = std::move(subject);
    principal_.scopes = std::move(scopes);
}

bool StaticTokenVerifier::ConstantTimeEqual(const std::string& lhs, const std::string& rhs) noexcept {
    const std::size_t max_size = std::max(lhs.size(), rhs.size());
    unsigned char diff = static_cast<unsigned char>(lhs.size() ^ rhs.size());
    for (std::size_t i = 0; i < max_size; ++i) {
        const unsigned char a = i < lhs.size() ? static_cast<unsigned char>(lhs[i]) : 0;
        const unsigned char b = i < rhs.size() ? static_cast<unsigned char>(rhs[i]) : 0;
        diff |= static_cast<unsigned char>(a ^ b);
    }
    return diff == 0;
}

AuthResult StaticTokenVerifier::Verify(const std::string& authorization_header) const {
    constexpr const char* kPrefix = "Bearer ";
    if (authorization_header.rfind(kPrefix, 0) != 0) {
        return {false, {}, "missing bearer token"};
    }
    const std::string candidate = authorization_header.substr(7);
    if (candidate.empty() || !ConstantTimeEqual(candidate, token_)) {
        return {false, {}, "invalid bearer token"};
    }
    return {true, principal_, {}};
}

}  // namespace tinyimx::mcp
