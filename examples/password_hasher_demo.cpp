#include "common/security/PasswordHasher.h"

#include <iostream>
#include <string>

int main() {
    tinyimx::PasswordHasher hasher;

    const std::string password = "demo-password";
    const std::string wrong_password = "wrong-password";

    std::cout << "========== Password Hasher Demo ==========\n";

    const std::string salt = hasher.GenerateSaltHex();
    const std::string hash =
        hasher.HashPassword(password, salt);

    std::cout << "password = " << password << '\n';
    std::cout << "salt = " << salt << '\n';
    std::cout << "hash = " << hash << '\n';

    const bool verify_correct =
        hasher.VerifyPassword(password, salt, hash);

    const bool verify_wrong =
        hasher.VerifyPassword(wrong_password, salt, hash);

    std::cout << "verify_correct_password = "
              << verify_correct << '\n';

    std::cout << "verify_wrong_password = "
              << verify_wrong << '\n';

    if (!verify_correct) {
        std::cerr << "correct password verify failed\n";
        return 1;
    }

    if (verify_wrong) {
        std::cerr << "wrong password verify unexpectedly passed\n";
        return 1;
    }

    std::cout << "Password hasher demo finished\n";
    std::cout << "==========================================\n";

    return 0;
}