#pragma once

#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

struct TestFailure : public std::exception {
    explicit TestFailure(std::string message) : message_(std::move(message)) {}

    const char* what() const noexcept override {
        return message_.c_str();
    }

private:
    std::string message_;
};

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& test_registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct TestRegistrar {
    TestRegistrar(const char* name, std::function<void()> fn) {
        test_registry().push_back(TestCase{name, std::move(fn)});
    }
};

#define TEST(name)                                                                                 \
    void name();                                                                                   \
    static TestRegistrar registrar_##name(#name, name);                                            \
    void name()

#define EXPECT_TRUE(condition)                                                                     \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            throw TestFailure(std::string("Expected true: ") + #condition);                        \
        }                                                                                          \
    } while (0)

#define EXPECT_FALSE(condition) EXPECT_TRUE(!(condition))

#define EXPECT_EQ(lhs, rhs)                                                                        \
    do {                                                                                           \
        if ((lhs) != (rhs)) {                                                                      \
            throw TestFailure(std::string("Expected equality for ") + #lhs + " and " + #rhs);     \
        }                                                                                          \
    } while (0)

#define EXPECT_NE(lhs, rhs)                                                                        \
    do {                                                                                           \
        if ((lhs) == (rhs)) {                                                                      \
            throw TestFailure(std::string("Expected inequality for ") + #lhs + " and " + #rhs);   \
        }                                                                                          \
    } while (0)

#define EXPECT_NULL(ptr) EXPECT_TRUE((ptr) == nullptr)
#define EXPECT_NOT_NULL(ptr) EXPECT_TRUE((ptr) != nullptr)

#define EXPECT_GE(lhs, rhs)                                                                          \
    do {                                                                                             \
        if (!((lhs) >= (rhs))) {                                                                     \
            throw TestFailure(std::string("Expected ") + #lhs + " >= " + #rhs);                     \
        }                                                                                            \
    } while (0)

#define EXPECT_LE(lhs, rhs)                                                                          \
    do {                                                                                             \
        if (!((lhs) <= (rhs))) {                                                                     \
            throw TestFailure(std::string("Expected ") + #lhs + " <= " + #rhs);                      \
        }                                                                                            \
    } while (0)

inline int run_all_tests() {
    const auto& tests = test_registry();
    int failures = 0;

    std::cout << "Running " << tests.size() << " tests...\n\n";

    for (const TestCase& test : tests) {
        std::cout << "[ RUN      ] " << test.name << '\n';
        try {
            test.fn();
            std::cout << "[       OK ] " << test.name << '\n';
        } catch (const TestFailure& failure) {
            ++failures;
            std::cout << "[  FAILED  ] " << test.name << " (" << failure.what() << ")\n";
        } catch (const std::exception& ex) {
            ++failures;
            std::cout << "[  FAILED  ] " << test.name << " (unexpected exception: " << ex.what()
                      << ")\n";
        }
    }

    std::cout << '\n';
    if (failures == 0) {
        std::cout << "All tests passed.\n";
    } else {
        std::cout << failures << " test(s) failed.\n";
    }

    return failures == 0 ? 0 : 1;
}
