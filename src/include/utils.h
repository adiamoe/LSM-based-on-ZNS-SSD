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
    vector<pair<uint64_t , uint64_t>> valid_data_offset;
    zone_meta(uint64_t _s, uint64_t _w, uint64_t _e, uint64_t _n):start(_s), write_ptr(_w), end(_e), valid_table_num(_n) {}
};

//储存数据的内存管理器
class MemoryManager {
private:
    //储存每一层SSTable的start pos
    typedef uint64_t start;
    start end_ptr;
    std::vector<start> unusedPage; //清空的页表
    std::vector<std::map<int, start>> mappingTable; //各个页表位置
    FemuCtrl *femu;
    //缓存读上来的页，避免重复读取
    LRUCache pageCache;
    vector<zone_meta> zone_metas;
    uint32_t zone_num;
    uint8_t open_zone_num;
    vector<uint8_t> open_zone;

    ThreadPool pool;
public:
    MemoryManager(): pageCache(512), pool(MAXIMUM_NUM_OF_OPEN_ZONE){

        femu = femu_init(SSD_SIZE, false, true);
        mappingTable.emplace_back();

        auto *meta = new uint64_t[2];
        get_zns_meta(meta);         //获取zns的一些基本信息
        uint64_t zone_size = meta[0];
        zone_num = SSD_SIZE / zone_size;
        for(unsigned i=0; i<zone_num; ++i) {
            zone_metas.emplace_back(i*zone_size, 0, (i+1)*zone_size-1, 0);
        }

        open_zone_num = MAXIMUM_NUM_OF_OPEN_ZONE;
        //先打开可以完全并行的zone，即各个zone所处的die均不同
        for(int i=0; i<open_zone_num/meta[1]; ++i) {
            for(int j=0; j<meta[1]; ++j)
                open_zone.emplace_back(j + zone_num/open_zone_num * i);
        }
    }

    void writeTable(int level, int num) {
        start s = end_ptr;
        if(!unusedPage.empty()) {
            s = unusedPage.back();
            unusedPage.pop_back();
        } else {
            s = end_ptr;
            end_ptr += DEFAULT_SSTABLE_SIZE;
        }
        if(mappingTable.size()<level+1)
            mappingTable.emplace_back();
        mappingTable[level][num] = s;
        femu_write(femu, nullptr, DEFAULT_SSTABLE_SIZE, s, nullptr);
    }

    //读整个sstable，不存cache，避免cache被污染
    void readTable(int level, int num, uint64_t offset, uint64_t length) {
        start s = mappingTable[level][num];
        uint32_t left = (s + offset) / DEFAULT_PAGE_SIZE * DEFAULT_PAGE_SIZE;  //对齐页边界
        uint32_t right = ((s + offset + length) / DEFAULT_PAGE_SIZE + 1)* DEFAULT_PAGE_SIZE;
        femu_read(femu, nullptr, right-left, left, nullptr);
    }

    //读单个数据时，缓存读取的页
    void getValue(int level, int num, uint64_t offset, uint64_t length) {
        start s = mappingTable[level][num];
        uint32_t left = (s + offset) / DEFAULT_PAGE_SIZE;  //对齐页边界
        uint32_t right = (s + offset + length) / DEFAULT_PAGE_SIZE;
        uint32_t batch_left = -1, batch_right = -1;
        vector<pair<uint32_t, uint32_t>> interval;
        uint32_t page = left;
        //对已经缓存的页，直接跳过
        for(; page <= right; ++page) {
            if(pageCache.get(page)==-1)
                break;
        }
        //遇到没有缓存的页，记录区间并缓存
        for(; page <= right; ++page) {
            if(pageCache.get(page)==-1) {
                if(batch_left == -1)
                    batch_left = page;
                batch_right = page;
                pageCache.put(page, page);
            } else if(batch_left != -1) {
                interval.emplace_back(batch_left, batch_right);
                batch_left = -1;
            }
        }
        if(batch_left != -1)
            interval.emplace_back(batch_left, batch_right);

        //读缓存的区间
        for(auto &it:interval) {
            uint64_t len = (it.second-it.first+1) * DEFAULT_PAGE_SIZE;
            uint64_t oft = it.first * DEFAULT_PAGE_SIZE;
            femu_read(femu, nullptr, len, oft, nullptr);
        }
    }

    //对于删除的sstable，需要清空对应的缓存
    void deleteTable(int level, int num) {
        start s = mappingTable[level][num];
        mappingTable[level].erase(num);
        unusedPage.push_back(s);
        uint32_t left = s / DEFAULT_PAGE_SIZE;  //对齐页边界
        uint32_t right = (s + DEFAULT_SSTABLE_SIZE) / DEFAULT_PAGE_SIZE;
        for(uint32_t page = left; page <= right; ++page) {
            pageCache.del(page);
        }
    }

};

#endif //FEMU_SIM_UTILS_H
