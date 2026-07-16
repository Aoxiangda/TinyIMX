#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace tinyimx::test {

class TestFailure : public std::runtime_error {
public:
    explicit TestFailure(const std::string& message)
        : std::runtime_error(message) {}
};

inline void Fail(const std::string& message,
                 const char* file,
                 int line) {
    std::ostringstream oss;
    oss << file << ":" << line << " " << message;
    throw TestFailure(oss.str());
}

template <typename T, typename = void>
struct IsStreamable : std::false_type {};

template <typename T>
struct IsStreamable<
    T,
    std::void_t<decltype(std::declval<std::ostream&>()
                         << std::declval<const T&>())>
> : std::true_type {};

template <typename T>
std::string ValueToString(const T& value) {
    std::ostringstream oss;

    if constexpr (IsStreamable<T>::value) {
        oss << value;
        return oss.str();
    } else if constexpr (std::is_enum_v<T>) {
        using UnderlyingType = std::underlying_type_t<T>;
        oss << static_cast<UnderlyingType>(value);
        return oss.str();
    } else {
        return "<unprintable>";
    }
}

inline void ExpectTrue(bool condition,
                       const char* expression,
                       const char* file,
                       int line) {
    if (!condition) {
        std::ostringstream oss;
        oss << "EXPECT_TRUE failed: " << expression;
        Fail(oss.str(), file, line);
    }
}

template <typename L, typename R>
inline void ExpectEq(const L& lhs,
                     const R& rhs,
                     const char* lhs_expr,
                     const char* rhs_expr,
                     const char* file,
                     int line) {
    if (!(lhs == rhs)) {
        std::ostringstream oss;
        oss << "EXPECT_EQ failed: " << lhs_expr << " != " << rhs_expr
            << ", lhs=" << ValueToString(lhs)
            << ", rhs=" << ValueToString(rhs);
        Fail(oss.str(), file, line);
    }
}

template <typename L, typename R>
inline void ExpectGe(const L& lhs,
                     const R& rhs,
                     const char* lhs_expr,
                     const R*,
                     const char* file,
                     int line) = delete;

template <typename L, typename R>
inline void ExpectGe(const L& lhs,
                     const R& rhs,
                     const char* lhs_expr,
                     const char* rhs_expr,
                     const char* file,
                     int line) {
    if (!(lhs >= rhs)) {
        std::ostringstream oss;
        oss << "EXPECT_GE failed: " << lhs_expr << " < " << rhs_expr
            << ", lhs=" << ValueToString(lhs)
            << ", rhs=" << ValueToString(rhs);
        Fail(oss.str(), file, line);
    }
}

class TestRunner {
public:
    using TestFunc = std::function<void()>;

    void Add(const std::string& name, TestFunc func) {
        tests_.push_back({name, std::move(func)});
    }

    int RunAll(std::string_view suite_name) {

        std::cout << "========== " << suite_name << " ==========\n";

        int failed_count = 0;


        for (const auto& test : tests_) {
            try {
                test.func();
                std::cout << "[PASS] " << test.name << '\n';
            } catch (const std::exception& e) {
                ++failed_count;
                std::cout << "[FAIL] " << test.name
                          << " : " << e.what() << '\n';
            } catch (...) {
                ++failed_count;
                std::cout << "[FAIL] " << test.name
                          << " : unknown exception\n";
            }
        }

        std::cout << "==============================================\n";
        std::cout << "total = " << tests_.size()
                  << ", failed = " << failed_count << '\n';

        return failed_count;
    }

private:
    struct TestCase {
        std::string name;
        TestFunc func;
    };

    std::vector<TestCase> tests_;
};

}  // namespace tinyimx::test

#define TINYIMX_EXPECT_TRUE(expr) ::tinyimx::test::ExpectTrue((expr), #expr, __FILE__, __LINE__)
#define TINYIMX_EXPECT_EQ(lhs, rhs) ::tinyimx::test::ExpectEq((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__)
#define TINYIMX_EXPECT_GE(lhs, rhs) ::tinyimx::test::ExpectGe((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__)
//#define TINYIMX_EXPECT_THROW(statement) do { bool t=false; try{statement;}catch(...){t=true;} if(!t) ::tinyimx::test::Fail("EXPECT_THROW failed: " #statement, __FILE__, __LINE__); } while(false)
#define TINYIMX_EXPECT_TRUE(expr) \
    ::tinyimx::test::ExpectTrue((expr), #expr, __FILE__, __LINE__)

#define TINYIMX_EXPECT_EQ(lhs, rhs) \
    ::tinyimx::test::ExpectEq((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__)

#define TINYIMX_EXPECT_GE(lhs, rhs) \
    ::tinyimx::test::ExpectGe((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__)

#define TINYIMX_EXPECT_THROW(statement)                                         \
    do {                                                                        \
        bool tinyimx_throw_caught = false;                                      \
        try {                                                                   \
            statement;                                                          \
        } catch (...) {                                                         \
            tinyimx_throw_caught = true;                                        \
        }                                                                       \
        if (!tinyimx_throw_caught) {                                            \
            ::tinyimx::test::Fail(                                              \
                std::string("EXPECT_THROW failed: ") + #statement,              \
                __FILE__,                                                       \
                __LINE__                                                        \
            );                                                                  \
        }                                                                       \
    } while (false)

// 这里必须多留一行！！！