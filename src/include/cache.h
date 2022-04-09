//
// Created by ZJW on 2022/3/7.
//

#ifndef FEMU_SIM_CACHE_H
#define FEMU_SIM_CACHE_H

#include <unordered_map>
#include <list>
#include <vector>
#include <queue>
#include <mutex>

using std::pair;
using std::list;
using std::unordered_map;
using std::queue;
using std::vector;
using std::mutex;
using std::lock_guard;

class LRUCache {
public:
    explicit LRUCache(int capacity) : cap(capacity) {}

    uint64_t get(uint64_t key) {
        if (map.find(key) == map.end())
            return -1;
        lock_guard<mutex> lk(lock);
        auto key_value = *map[key];
        cache.erase(map[key]);
        cache.push_front(key_value);
        map[key] = cache.begin();
        return key_value;
    }

    void put(uint64_t key) {
        lock_guard<mutex> lk(lock);
        if (map.find(key) == map.end()) {
            if (cache.size() == cap) {
                map.erase(cache.back());
                cache.pop_back();
            }
        }
        else {
            cache.erase(map[key]);
        }
        cache.push_front(key);
        map[key] = cache.begin();
    }

    void del(uint64_t key) {
        if(map.find(key) == map.end())
            return;
        lock_guard<mutex> lk(lock);
        cache.erase(map[key]);
        map.erase(key);
    }
private:
    int cap;
    mutex lock;
    list<uint64_t> cache{};
    unordered_map<uint64_t, list<uint64_t>::iterator> map{};
};

class queue_cache {
private:
    vector<queue<uint32_t>> queues;
    uint32_t ptr;
    uint32_t div;
    uint32_t size;
public:
    queue_cache(uint32_t num, uint32_t div_, uint32_t total):queues(num), ptr(0), div(div_), size(total) {
        for(int i=0; i<total; ++i) {
            uint32_t index = (i/2/div) * 2 + i%2;
            queues[index].emplace(i);
        }
        vector<int> nums;
        for(auto &queue:queues) {
            nums.emplace_back(queue.size());
        }
    }
    
    bool empty() const {
        return size == 0;
    }
    
    uint32_t get() {
        while(!empty() && queues[ptr].empty()) {
            ptr = (++ptr) % queues.size();
        }
        auto val = queues[ptr].front();
        queues[ptr].pop();
        ptr = (++ptr) % queues.size();
        size--;
        return val;
    }
    
    void put(uint32_t order) {
        uint32_t index = (order/2/div)*2 + order%2;
        queues[index].emplace(order);
        size++;
    }
    
    uint32_t getsize() const {
        return size;
    }
    
};

#endif //FEMU_SIM_CACHE_H
