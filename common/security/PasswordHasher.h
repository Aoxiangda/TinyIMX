#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tinyimx {

class PasswordHasher {
public:
    PasswordHasher() = default;

    PasswordHasher(const PasswordHasher&) = delete;
    PasswordHasher& operator=(const PasswordHasher&) = delete;

    std::string GenerateSaltHex(std::size_t byte_count = 16) const;

    std::string HashPassword(const std::string& password,
                             const std::string& salt_hex) const;

    bool VerifyPassword(const std::string& password,
                        const std::string& salt_hex,
                        const std::string& expected_hash_hex) const;

private:
    std::string HexEncode(const std::vector<std::uint8_t>& data) const;

    std::vector<std::uint8_t> HexDecode(const std::string& hex) const;

    bool ConstantTimeEquals(const std::string& lhs,
                            const std::string& rhs) const;

private:
    static constexpr int kIterations = 100000;
    static constexpr int kHashBytes = 32;
};

}  // namespace tinyimx