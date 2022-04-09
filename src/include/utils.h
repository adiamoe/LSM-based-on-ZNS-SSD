//
// Created by ZJW on 2022/2/25.
//
#ifndef FEMU_SIM_UTILS_H
#define FEMU_SIM_UTILS_H

#include <threadPool.h>
#include <cache.h>
#include <femu.h>
#include <cmath>
#include <map>
#include <vector>
#include <iostream>
#include <cassert>
#include <algorithm>
#include <unordered_set>

using std::vector;
using std::queue;
using std::pair;
using std::map;
using std::unordered_set;

#define MAXIMUM_NUM_OF_OPEN_ZONE 17

const uint64_t KiB = 1024;
const uint64_t MiB = 1024 * KiB;
const uint64_t GiB = 1024 * MiB;
const uint64_t DEFAULT_SSTABLE_SIZE = 2 * MiB;
const uint64_t DEFAULT_PAGE_SIZE = 4 * KiB;

struct zone_meta{
    uint64_t start;
    uint64_t write_ptr;
    uint64_t end;
    uint16_t valid_table_num;
    vector<uint64_t> deleted_table;     //zone中被删除table的起始位置
    zone_meta(uint64_t _s, uint64_t _w, uint64_t _e, uint64_t _n):start(_s), write_ptr(_w), end(_e), valid_table_num(_n) {}
};

//储存数据的内存管理器
class MemoryManager {
private:
    vector<std::map<int, uint64_t>> mappingTable; //各个页表位置
    map<uint64_t, pair<int, int>> remappingTable; //各个位置对应的页表
    vector<std::map<int, std::future<int>>> completeTable;
    FemuCtrl *femu;
    //缓存读上来的页，避免重复读取
    LRUCache pageCache;
    vector<zone_meta> zone_metas;
    uint32_t zone_num;
    uint64_t zone_cap;
    uint16_t open_zone_num;

    vector<uint32_t> open_zone;
    int which_zone;

    uint64_t ssd_capactity;
    queue_cache *empty_zone;
    unordered_set<uint64_t> full_zone;
    unordered_set<uint32_t> reset_zone;

    ThreadPool pool;

    uint64_t reset_times;
public:
    MemoryManager(): pageCache(5120), pool(MAXIMUM_NUM_OF_OPEN_ZONE+1){
        auto *meta = new uint64_t[4];
        get_zns_meta(meta);         //获取zns的一些基本信息
        ssd_capactity = meta[3];

        femu = femu_init(ssd_capactity, false, true);
        mappingTable.emplace_back();
        completeTable.emplace_back();

        zone_cap = meta[0];
        zone_num = ssd_capactity / zone_cap;
        for(unsigned i=0; i<zone_num; ++i) {
            zone_metas.emplace_back(i*zone_cap, i*zone_cap, (i+1)*zone_cap-1, 0);
        }

        open_zone_num = MAXIMUM_NUM_OF_OPEN_ZONE;

        unsigned queue_num = zone_num / meta[2];
        empty_zone = new queue_cache(queue_num, meta[2], zone_num);

        for(int i=0; i<open_zone_num; ++i) {
            open_zone.emplace_back(empty_zone->get());
        }
        which_zone = 0;
        reset_times = 0;
    }

    ~MemoryManager() {
        printf("The number of Zone Reset is %lu\n", reset_times);
        delete empty_zone;
    }

    void writeTable(int level, int num);

    //读整个sstable，不存cache，避免cache被污染
    void readTable(int level, int num, uint64_t offset, uint64_t length);

    //读单个数据时，缓存读取的页
    void getValue(int level, int num, uint64_t offset, uint64_t length);

    //对于删除的sstable，需要清空对应的缓存
    void deleteTable(int level, int num);

    //清空对应的zone
    void resetZone(unsigned zone_order, bool force);

    void evictZone();

    void tryReset();
};

#define zone_err(fmt, ...) \
    do { fprintf(stderr, "[ZNS] Zone-Err: " fmt, ## __VA_ARGS__); } while (0)

#endif //FEMU_SIM_UTILS_H
