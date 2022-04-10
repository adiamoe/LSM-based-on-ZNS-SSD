#include "test.h"
#include <algorithm>

class correctnessTest : public Test {
private:
    const uint64_t length = 3000;
    enum test_type {throughput = 0, latency = 1, correctness = 2};
    test_type type = throughput;

    void data_preparation(vector<pair<bool, uint64_t>> &run_data) {
        std::fstream load("../../src/load.dat");
        std::fstream run("../../src/run.dat");
        if(!load.is_open() || !run.is_open())
            cerr<<"file is not open correctly\n";

        cout<<"Reading File Data\n";
        vector<uint64_t> load_data;
        char buffer[128];
        while(!load.eof()) {
            load.getline(buffer, 128, '\n');
            load_data.emplace_back(stoull(buffer));
        }

        while(!run.eof()) {
           run.getline(buffer, 128, '\n');
           string s = buffer;
           auto pos = s.find(' ');
           string operation = s.substr(0, pos);
           string num = s.erase(0, pos+1);
           if(operation == "READ")
               run_data.emplace_back(true, stoul(num));
           else if(operation == "UPDATE")
               run_data.emplace_back(false, stoul(num));
           else
               cerr<<"Not have such operation\n";
        }

        cout<<"Loading data\n";
        string init(length, '0');
        start = chrono::steady_clock::now();
        for(uint64_t i : load_data)
            store.put(i, init);

        phase();
    }

    void Latency_test() {
        vector<pair<bool, uint64_t>> run_data;
        data_preparation(run_data);
        cout<<"Running and testing\n";
        string update(length, '1');
        vector<double> get_time;
        vector<double> put_time;
        for(auto &it : run_data) {
            if (it.first) {
                auto begin = chrono::steady_clock::now();
                store.get(it.second);
                auto end = chrono::steady_clock::now();
                auto elapsed = end - begin;
                get_time.emplace_back(chrono::duration<double, micro>(elapsed).count());
            } else {
                auto begin = chrono::steady_clock::now();
                store.put(it.second, update);
                auto end = chrono::steady_clock::now();
                auto elapsed = end - begin;
                put_time.emplace_back(chrono::duration<double, micro>(elapsed).count());
            }
        }
        sort(get_time.begin(), get_time.end());
        sort(put_time.begin(), put_time.end());

        vector<double> get_statistic;
        vector<double> put_statistic;

        //计算平均值
        double get_average = accumulate(get_time.begin(), get_time.end(), 0.0) / (double) get_time.size();
        double put_average = accumulate(put_time.begin(), put_time.end(), 0.0) / (double) put_time.size();
        get_statistic.emplace_back(get_average);
        put_statistic.emplace_back(put_average);

        //计算中位数
        get_statistic.emplace_back(get_time[lround(get_time.size()/2)]);
        put_statistic.emplace_back(put_time[lround(put_time.size()/2)]);

        //计算90分位
        get_statistic.emplace_back(get_time[lround((double) get_time.size()*0.9)]);
        put_statistic.emplace_back(put_time[lround((double) put_time.size()*0.9)]);

        //计算95分位
        get_statistic.emplace_back(get_time[lround((double) get_time.size()*0.95)]);
        put_statistic.emplace_back(put_time[lround((double) put_time.size()*0.95)]);

        //计算99分位
        get_statistic.emplace_back(get_time[lround((double) get_time.size()*0.99)]);
        put_statistic.emplace_back(put_time[lround((double) put_time.size()*0.99)]);

        //计算99999分位
        get_statistic.emplace_back(get_time[lround((double) get_time.size()*0.99999)]);
        put_statistic.emplace_back(put_time[lround((double) put_time.size()*0.99999)]);

        cout<<"get latency:\t";
        for(auto i:get_statistic) {
            cout<<i<<"\t";
        }
        cout<<endl<<"put latency:\t";
        for(auto i:put_statistic) {
            cout<<i<<"\t";
        }
    }

    void throughput_test() {
        vector<pair<bool, uint64_t>> run_data;
        data_preparation(run_data);
        cout<<"Running and testing\n";
        string update(length, '1');
        start = chrono::steady_clock::now();
        for(auto &it : run_data)
            if(it.first)
                store.get(it.second);
            else
                store.put(it.second, update);
        phase();
    }

    void Correctness_test() {
        const int Min = 0;
        const int Max = 1024 * 80;
        int i;

        start = chrono::steady_clock::now();
        for (i = Min; i < Max; ++i) {
            store.put(i, std::string(i + 1, 's'));
            EXPECT(std::string(i + 1, 's'), store.get(i));
        }
        phase();

        start = chrono::steady_clock::now();
        for (i = Min + 1; i < Max; i += 4) {
            store.put(i, std::string(i + 1, 't'));
            EXPECT(std::string(i, 's'), store.get(i - 1));
        }
        phase();

        start = chrono::steady_clock::now();
        for (i = Min; i < Max; i += 2)
            EXPECT(std::string(i + 1, 's'), store.get(i));
        phase();

        start = chrono::steady_clock::now();
        for (i = Min+1; i < Max; i += 4)
            EXPECT(std::string(i + 1, 't'), store.get(i));
        phase();
    }

public:
    correctnessTest() : Test() {}

    void start_test() override {
        switch(type) {
            case throughput:
                std::cout << "KVStore Throughput Test" << std::endl;
                throughput_test();
                break;
            case latency:
                std::cout << "KVStore Latency Test" << std::endl;
                Latency_test();
                break;
            case correctness:
                std::cout << "KVStore Correctness Test" << std::endl;
                Correctness_test();
                break;
        }
    }
};

int main() {
    correctnessTest test;
    test.start_test();
    return 0;
}
