//
// Created by ZJW on 2022/2/25.
//
#ifndef FEMU_SIM_UTILS_H
#define FEMU_SIM_UTILS_H

#include <threadPool.h>
#include <LRUCache.h>
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

#define MAXIMUM_NUM_OF_OPEN_ZONE 10

const uint64_t KiB = 1024;
const uint64_t MiB = 1024 * KiB;
const uint64_t GiB = 1024 * MiB;
const uint64_t DEFAULT_SSTABLE_SIZE = 2 * MiB;
const uint64_t DEFAULT_PAGE_SIZE = 4 * KiB;
const uint64_t SSD_SIZE (8 * GiB);

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
    //储存每一层SSTable的start pos
    typedef uint64_t start;
    vector<start> unusedPage; //清空的页表
    vector<std::map<int, start>> mappingTable; //各个页表位置
    map<start, pair<int, int>> remappingTable; //各个位置对应的页表
    vector<std::map<int, std::future<int>>> completeTable;
    FemuCtrl *femu;
    //缓存读上来的页，避免重复读取
    LRUCache pageCache;
    vector<zone_meta> zone_metas;
    uint32_t zone_num;
    uint8_t open_zone_num;

    vector<uint8_t> open_zone;
    int which_zone;

    queue<uint64_t> empty_zone;
    unordered_set<uint64_t> full_zone;

    ThreadPool pool;
public:
    MemoryManager(): pageCache(512), pool(MAXIMUM_NUM_OF_OPEN_ZONE){

        femu = femu_init(SSD_SIZE, false, true);
        mappingTable.emplace_back();
        completeTable.emplace_back();

        auto *meta = new uint64_t[4];
        get_zns_meta(meta);         //获取zns的一些基本信息
        uint64_t zone_size = meta[0];
        zone_num = SSD_SIZE / zone_size;
        for(unsigned i=0; i<zone_num; ++i) {
            zone_metas.emplace_back(i*zone_size, i*zone_size, (i+1)*zone_size-1, 0);
        }

        open_zone_num = MAXIMUM_NUM_OF_OPEN_ZONE;

        //先打开可以完全并行的zone，即各个zone所处的die均不同
        unsigned int blocks_per_die = meta[2] * meta[3];
        for(int i=0; i<zone_num/meta[1]; ++i) {
            for(int j=0; j<meta[1]; ++j) {
                empty_zone.emplace(j + (meta[2] * i) % blocks_per_die + i/meta[3]*2);
            }
        }

        for(int i=0; i<open_zone_num; ++i) {
            open_zone.emplace_back(empty_zone.front());
            empty_zone.pop();
        }
        which_zone = 0;
    }

    void writeTable(int level, int num);

    //读整个sstable，不存cache，避免cache被污染
    void readTable(int level, int num, uint64_t offset, uint64_t length);

    //读单个数据时，缓存读取的页
    void getValue(int level, int num, uint64_t offset, uint64_t length);

    //对于删除的sstable，需要清空对应的缓存
    void deleteTable(int level, int num);

    //清空对应的zone
    void resetZone(int zone_order);
};

#endif //FEMU_SIM_UTILS_H
