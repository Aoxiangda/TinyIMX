#include "common/security/PasswordHasher.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace tinyimx {

std::string PasswordHasher::GenerateSaltHex(
    std::size_t byte_count
) const {
    if (byte_count == 0) {
        throw std::invalid_argument("salt byte count must be positive");
    }

    std::vector<std::uint8_t> salt(byte_count);

    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }

    return HexEncode(salt);
}

std::string PasswordHasher::HashPassword(
    const std::string& password,
    const std::string& salt_hex
) const {
    if (password.empty()) {
        throw std::invalid_argument("password is empty");
    }

    const std::vector<std::uint8_t> salt = HexDecode(salt_hex);

    if (salt.empty()) {
        throw std::invalid_argument("salt is empty");
    }

    std::vector<std::uint8_t> output(kHashBytes);

    const int ok = PKCS5_PBKDF2_HMAC(
        password.data(),
        static_cast<int>(password.size()),
        salt.data(),
        static_cast<int>(salt.size()),
        kIterations,
        EVP_sha256(),
        static_cast<int>(output.size()),
        output.data()
    );

    if (ok != 1) {
        throw std::runtime_error("PKCS5_PBKDF2_HMAC failed");
    }

    return HexEncode(output);
}

bool PasswordHasher::VerifyPassword(
    const std::string& password,
    const std::string& salt_hex,
    const std::string& expected_hash_hex
) const {
    if (password.empty() ||
        salt_hex.empty() ||
        expected_hash_hex.empty()) {
        return false;
    }

    try {
        const std::string actual_hash =
            HashPassword(password, salt_hex);

        return ConstantTimeEquals(
            actual_hash,
            expected_hash_hex
        );
    } catch (...) {
        return false;
    }
}

std::string PasswordHasher::HexEncode(
    const std::vector<std::uint8_t>& data
) const {
    std::ostringstream oss;

    for (std::uint8_t byte : data) {
        oss << std::hex
            << std::setw(2)
            << std::setfill('0')
            << static_cast<int>(byte);
    }

    return oss.str();
}

std::vector<std::uint8_t> PasswordHasher::HexDecode(
    const std::string& hex
) const {
    if (hex.size() % 2 != 0) {
        throw std::invalid_argument("invalid hex length");
    }

    std::vector<std::uint8_t> data;
    data.reserve(hex.size() / 2);

    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const std::string byte_string = hex.substr(i, 2);

        const auto value = static_cast<std::uint8_t>(
            std::stoul(byte_string, nullptr, 16)
        );

        data.push_back(value);
    }

    return data;
}

bool PasswordHasher::ConstantTimeEquals(
    const std::string& lhs,
    const std::string& rhs
) const {
    if (lhs.size() != rhs.size()) {
        return false;
    }

    unsigned char diff = 0;

    for (std::size_t i = 0; i < lhs.size(); ++i) {
        diff |= static_cast<unsigned char>(lhs[i] ^ rhs[i]);
    }

    return diff == 0;
}

}  // namespace tinyimx