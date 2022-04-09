//
// Created by ZJW on 2022/2/27.
//

#ifndef FEMU_SIM_TEST_H
#define FEMU_SIM_TEST_H

#include <iostream>
#include <cstdint>
#include <string>
#include <chrono>
#include <kvstore.h>
#include <unordered_map>
#include <random>

using std::string;
using std::random_device;
using std::default_random_engine;

unordered_multimap<uint64_t, string> generator(uint64_t len, uint64_t size) {
    unordered_multimap<uint64_t, string> vec;
    uint64_t key;
    unsigned tmp;
    char ch;
    string value;

    random_device rd;
    default_random_engine random(rd());

    for(int k = 0; k < size; ++k){
        key = random();
        for (int i = 0; i < len; ++i) {
            tmp = random() % 36;    // 随机一个小于 36 的整数，0-9、A-Z 共 36 种字符
            if (tmp < 10) {
                ch = tmp + '0';
            } else {
                tmp -= 10;
                ch = tmp + 'a';
            }
            value += ch;
        }
        vec.emplace(key, value);
        value.clear();
    }
    return vec;
}

class Test {
protected:
    uint64_t nr_tests;
    uint64_t nr_passed_tests;
    uint64_t nr_phases;
    uint64_t nr_passed_phases;
    std::chrono::steady_clock::time_point start;

#define EXPECT(exp, got) expect<decltype(got)>((exp), (got))

    template<class T>
    void expect(const T &exp, const T &got) {
        ++nr_tests;
        if (exp == got) {
            ++nr_passed_tests;
        }
    }

    void phase() {

        auto end = std::chrono::steady_clock::now();
        auto elapsed = end - start;

        // Report
        std::cout << "Phase " << (nr_phases + 1) << ": ";
        if (nr_tests == nr_passed_tests)
            std::cout << "[PASS]" << " ";
        else
            std::cout << "[FAIL]" << " ";
        std::cout << nr_passed_tests << "/" << nr_tests << " ";
        std::cout << "Take: " << chrono::duration<double>(elapsed).count() << " seconds" << endl; // converts to seconds

        ++nr_phases;

        std::cout.flush();

        // Reset
        nr_tests = 0;
        nr_passed_tests = 0;
        start = chrono::steady_clock::now();
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
