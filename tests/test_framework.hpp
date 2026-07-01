#pragma once
#include <string>
#include <vector>
#include <functional>
#include <iostream>

struct TestRegistry {
    std::string name;
    std::function<void()> fn;
};

std::vector<TestRegistry>& get_tests();
bool register_test(const std::string& name, std::function<void()> fn);

#define TEST(suite, name) \
    void suite##_##name(); \
    const bool reg_##suite##_##name = register_test(#suite "." #name, suite##_##name); \
    void suite##_##name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "Assertion failed: " << #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "Assertion failed: " << #a << " == " << #b << " (actual: " << (a) << ", expected: " << (b) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            std::exit(1); \
        } \
    } while (0)
