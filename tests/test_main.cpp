#include "test_framework.hpp"
#include <iostream>
#include <exception>

std::vector<TestRegistry>& get_tests() {
    static std::vector<TestRegistry> tests;
    return tests;
}

bool register_test(const std::string& name, std::function<void()> fn) {
    get_tests().push_back({name, fn});
    return true;
}

int main() {
    std::cout << "Running fixure unit tests..." << std::endl;
    int run_count = 0;
    for (const auto& test : get_tests()) {
        std::cout << "  Running " << test.name << "..." << std::endl;
        try {
            test.fn();
            run_count++;
        } catch (const std::exception& e) {
            std::cerr << "Test " << test.name << " failed with exception: " << e.what() << std::endl;
            return 1;
        } catch (...) {
            std::cerr << "Test " << test.name << " failed with unknown exception" << std::endl;
            return 1;
        }
    }
    std::cout << "SUCCESS: All " << run_count << " tests passed." << std::endl;
    return 0;
}
