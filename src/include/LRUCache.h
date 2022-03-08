//
// Created by ZJW on 2022/3/7.
//

#ifndef FEMU_SIM_LRUCACHE_H
#define FEMU_SIM_LRUCACHE_H

#include <unordered_map>
#include <list>

class LRUCache {
public:
    explicit LRUCache(int capacity) : cap(capacity) {}

    uint32_t get(uint32_t key) {
        if (map.find(key) == map.end())
            return -1;
        auto key_value = *map[key];
        cache.erase(map[key]);
        cache.push_front(key_value);
        map[key] = cache.begin();
        return key_value.second;
    }

    void put(uint32_t key, uint32_t value) {
        if (map.find(key) == map.end()) {
            if (cache.size() == cap) {
                map.erase(cache.back().first);
                cache.pop_back();
            }
        }
        else {
            cache.erase(map[key]);
        }
        cache.push_front({key, value});
        map[key] = cache.begin();
    }

    void del(uint32_t key) {
        if(map.find(key) == map.end())
            return;
        cache.erase(map[key]);
        map.erase(key);
    }
private:
    int cap;
    list<pair<uint32_t, uint32_t>> cache{};
    unordered_map<uint32_t, list<pair<uint32_t, uint32_t>>::iterator> map{};
};

#endif //FEMU_SIM_LRUCACHE_H
