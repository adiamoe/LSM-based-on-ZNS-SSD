#include "test.h"

class correctnessTest : public Test {
private:
    const int Min = 0;
    const int Max = 1024 * 12;

    void regular_test() {

        int i;

        for (i = Min; i < Max; ++i) {
            store.put(i, std::string(i + 1, 's'));
            EXPECT(std::string(i + 1, 's'), store.get(i));
        }
        phase();

        for (i = Min + 1; i < Max; i += 4) {
            store.put(i, std::string(i + 1, 't'));
            EXPECT(std::string(i + 1, 't'), store.get(i));
            EXPECT(std::string(i, 's'), store.get(i - 1));
        }
        phase();

        for (i = Min; i < Max; i += 2)
            EXPECT(std::string(i + 1, 's'), store.get(i));
        phase();
    }

public:
    correctnessTest() : Test() {}

    void start_test() override {
        std::cout << "KVStore Correctness Test" << std::endl;
        regular_test();
    }
};

int main() {
    correctnessTest test;
    test.start_test();
    return 0;
}
