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
    vector<uint16_t> deleted_table;
    zone_meta(uint64_t _s, uint64_t _w, uint64_t _e, uint64_t _n):start(_s), write_ptr(_w), end(_e), valid_table_num(_n) {}
};

//储存数据的内存管理器
class MemoryManager {
private:
    //储存每一层SSTable的start pos
    typedef uint64_t start;
    std::vector<start> unusedPage; //清空的页表
    std::vector<std::map<int, start>> mappingTable; //各个页表位置
    std::map<start, pair<int, int>> remappingTable; //各个位置对应的页表
    std::vector<std::map<int, std::future<int>>> completeTable;
    FemuCtrl *femu;
    //缓存读上来的页，避免重复读取
    LRUCache pageCache;
    vector<zone_meta> zone_metas;
    uint32_t zone_num;
    uint8_t open_zone_num;

    vector<uint8_t> open_zone;
    int which_zone;

    queue<uint64_t> empty_zone;
    queue<uint64_t> full_zone;

    ThreadPool pool;
public:
    MemoryManager(): pageCache(512), pool(MAXIMUM_NUM_OF_OPEN_ZONE){

        femu = femu_init(SSD_SIZE, false, true);
        mappingTable.emplace_back();

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

void MemoryManager::getValue(int level, int num, uint64_t offset, uint64_t length) {
    if(completeTable[level].find(num) == completeTable[level].end() || completeTable[level][num].get() == 0) {
        start s = mappingTable[level][num];
        uint32_t left = (s + offset) / DEFAULT_PAGE_SIZE;  //对齐页边界
        uint32_t right = (s + offset + length) / DEFAULT_PAGE_SIZE;
        uint32_t batch_left = -1, batch_right = -1;
        vector<pair<uint32_t, uint32_t>> interval;
        uint32_t page = left;
        //对已经缓存的页，直接跳过
        for (; page <= right; ++page) {
            if (pageCache.get(page) == -1)
                break;
        }
        //遇到没有缓存的页，记录区间并缓存
        for (; page <= right; ++page) {
            if (pageCache.get(page) == -1) {
                if (batch_left == -1)
                    batch_left = page;
                batch_right = page;
                pageCache.put(page, page);
            } else if (batch_left != -1) {
                interval.emplace_back(batch_left, batch_right);
                batch_left = -1;
            }
        }
        if (batch_left != -1)
            interval.emplace_back(batch_left, batch_right);

        //读缓存的区间
        for (auto &it: interval) {
            uint64_t len = (it.second - it.first + 1) * DEFAULT_PAGE_SIZE;
            uint64_t oft = it.first * DEFAULT_PAGE_SIZE;
            femu_read(femu, len, oft, nullptr);
        }
    }
}

void MemoryManager::deleteTable(int level, int num) {
    assert(completeTable[level].find(num) == completeTable[level].end());
    start s = mappingTable[level][num];
    mappingTable[level].erase(num);
    remappingTable.erase(s);

    int order = s / (SSD_SIZE / zone_num);
    zone_metas[order].valid_table_num--;
    zone_metas[order].deleted_table.emplace_back(s);

    uint32_t left = s / DEFAULT_PAGE_SIZE;  //对齐页边界
    uint32_t right = (s + DEFAULT_SSTABLE_SIZE) / DEFAULT_PAGE_SIZE;
    for(uint32_t page = left; page <= right; ++page) {
        pageCache.del(page);
    }
}

void MemoryManager::readTable(int level, int num, uint64_t offset, uint64_t length) {
    if(completeTable[level].find(num) == completeTable[level].end() || completeTable[level][num].get() == 0) {
        completeTable[level].erase(num);
        start s = mappingTable[level][num];
        uint32_t left = (s + offset) / DEFAULT_PAGE_SIZE * DEFAULT_PAGE_SIZE;  //对齐页边界
        uint32_t right = ((s + offset + length) / DEFAULT_PAGE_SIZE + 1)* DEFAULT_PAGE_SIZE;
        femu_read(femu,right-left, left, nullptr);
    }
}

void MemoryManager::writeTable(int level, int num) {
    start s = zone_metas[open_zone[which_zone]].write_ptr;
    zone_meta &meta = zone_metas[open_zone[which_zone]];
    meta.write_ptr += DEFAULT_SSTABLE_SIZE;
    meta.valid_table_num++;
    if(meta.write_ptr >= meta.end) {
        full_zone.emplace(open_zone[which_zone]);
        open_zone[which_zone] = empty_zone.front();
        empty_zone.pop();
    }
    which_zone = (++which_zone) % 10;
    if(mappingTable.size()<level+1) {
        mappingTable.emplace_back();
        completeTable.emplace_back();
    }
    mappingTable[level][num] = s;
    remappingTable[s] = {level, num};
    completeTable[level][num] = pool.enqueue(which_zone, &femu_write, femu, DEFAULT_SSTABLE_SIZE, s, nullptr);
}

void MemoryManager::resetZone(int zone_order) {
    auto &meta = zone_metas[zone_order];

    //只清空写满的zone
    if(meta.write_ptr < meta.end)
        return;

    unsigned maximum_table = (SSD_SIZE / zone_num) / DEFAULT_SSTABLE_SIZE;
    double rate = (double) meta.valid_table_num / maximum_table;

    //利用率不足0.3时清空整个zone
    if(rate < 0.3) {
        uint64_t zone_size = SSD_SIZE / zone_num;
        int start_table = zone_order * zone_size;
        int end_table = (zone_order+1) * zone_size;
        vector<int> rewrite_table;
        //先排序，降低搜索的时间复杂度
        std::sort(meta.deleted_table.begin(), meta.deleted_table.end());
        for(int table = start_table, index = 0; table < end_table; table += DEFAULT_SSTABLE_SIZE) {
            if(meta.deleted_table[index] == table) {
                index++;
            } else {
                femu_read(femu, DEFAULT_SSTABLE_SIZE, table, nullptr);
                rewrite_table.emplace_back(table);
            }
        }
        unsigned to_zone = empty_zone.front();
        empty_zone.pop();

        //todo:是否有可能写满
        start s = zone_metas[open_zone[to_zone]].write_ptr;
        zone_meta &to_meta = zone_metas[open_zone[to_zone]];
        for(unsigned table = to_meta.write_ptr, i=0; table <= to_meta.end && i < rewrite_table.size();
                    table += DEFAULT_SSTABLE_SIZE, i++) {
            auto it = remappingTable[rewrite_table[i]];
            remappingTable.erase(rewrite_table[i]);
            mappingTable[it.first][it.second] = table;
            remappingTable[table] = it;
        }
        to_meta.write_ptr += meta.valid_table_num * DEFAULT_SSTABLE_SIZE;
        to_meta.valid_table_num += meta.valid_table_num;
        femu_write(femu, meta.valid_table_num * DEFAULT_SSTABLE_SIZE, s, nullptr);
        femu_reset(femu, zone_size, meta.start, nullptr);
    }
}

#endif //FEMU_SIM_UTILS_H
