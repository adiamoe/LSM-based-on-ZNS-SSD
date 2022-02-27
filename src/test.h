//
// Created by ZJW on 2022/2/27.
//

#ifndef FEMU_SIM_TEST_H
#define FEMU_SIM_TEST_H

#include <iostream>
#include <cstdint>
#include <string>

#include <kvstore.h>

class Test {
protected:
    uint64_t nr_tests;
    uint64_t nr_passed_tests;
    uint64_t nr_phases;
    uint64_t nr_passed_phases;

#define EXPECT(exp, got) expect<decltype(got)>((exp), (got))

    template<class T>
    void expect(const T &exp, const T &got) {
        ++nr_tests;
        if (exp == got) {
            ++nr_passed_tests;
        }
    }

    void phase() {

        // Report
        std::cout << "Phase " << (nr_phases + 1) << ": ";
        std::cout << nr_passed_tests << "/" << nr_tests << " ";

        // Count
        ++nr_phases;
        if (nr_tests == nr_passed_tests) {
            ++nr_passed_phases;
            std::cout << "[PASS]" << std::endl;
        } else
            std::cout << "[FAIL]" << std::endl;

        std::cout.flush();

        // Reset
        nr_tests = 0;
        nr_passed_tests = 0;
    }

    void report(void) {
        std::cout << nr_passed_phases << "/" << nr_phases << " passed.";
        std::cout << std::endl;
        std::cout.flush();

        nr_phases = 0;
        nr_passed_phases = 0;
    }

    class KVStore store;

public:
    Test() {
        nr_tests = 0;
        nr_passed_tests = 0;
        nr_phases = 0;
        nr_passed_phases = 0;
    }

    virtual void start_test() {
        std::cout << "No test is implemented." << std::endl;
    }
};

#endif //FEMU_SIM_TEST_H
