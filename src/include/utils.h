//
// Created by ZJW on 2022/2/25.
//
#ifndef FEMU_SIM_UTILS_H
#define FEMU_SIM_UTILS_H

#include <LRUCache.h>
#include <femu.h>
#include <cmath>
#include <map>
#include <vector>
#include <iostream>

const uint64_t KiB = 1024;
const uint64_t MiB = 1024 * KiB;
const uint64_t GiB = 1024 * MiB;
const uint64_t DEFAULT_SSTABLE_SIZE = 2 * MiB;
const uint64_t DEFAULT_PAGE_SIZE = 4 * KiB;
const uint64_t SSD_SIZE (8 * GiB);

//储存数据的内存管理器
class MemoryManager {
private:
    //储存每一层SSTable的start pos
    typedef uint64_t start;
    uint64_t end_ptr; //分配空间的最尾部
    std::vector<start> unusedPage; //清空的页表
    std::vector<std::map<int, start>> mappingTable; //各个页表位置
    FemuCtrl *femu;
    //缓存读上来的页，避免重复读取
    LRUCache pageCache;
public:
    MemoryManager(): pageCache(512) {
        femu = femu_init(SSD_SIZE, false, true);
        end_ptr = 0;
        mappingTable.emplace_back();
    }

    void writeTable(int level, int num) {
        start s;
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
        uint32_t left = (s + offset) / DEFAULT_PAGE_SIZE *DEFAULT_PAGE_SIZE;  //对齐页边界
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
