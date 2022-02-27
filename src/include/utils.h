//
// Created by ZJW on 2022/2/25.
//
#ifndef FEMU_SIM_UTILS_H
#define FEMU_SIM_UTILS_H

#include <femu.h>
#include <cmath>
#include <map>
#include <vector>

const int MEMTABLE = (int) pow(2, 21);
const uint64_t SSD_SIZE (8ULL * 1024 * 1024 * 1024);

//储存数据的内存管理器
class MemoryManager {
private:
    //储存每一层SSTable的start pos
    typedef uint64_t start;
    uint64_t end_ptr; //分配空间的最尾部
    std::vector<start> unusedPage; //清空的页表
    std::vector<std::map<int, start>> mappingTable; //各个页表位置
    FemuCtrl *femu;
public:
    MemoryManager() {
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
            end_ptr += MEMTABLE;
        }
        if(mappingTable.size()<level+1)
            mappingTable.emplace_back();
        mappingTable[level][num] = s;
        femu_write(femu, nullptr, MEMTABLE, s, nullptr);
    }

    void readTable(int level, int num, uint64_t offset, uint64_t length) {
        start s = mappingTable[level][num];
        femu_read(femu, nullptr, length, s+offset, nullptr);
    }

    void deleteTable(int level, int num) {
        start s = mappingTable[level][num];
        mappingTable[level].erase(num);
        unusedPage.push_back(s);
    }

};

#endif //FEMU_SIM_UTILS_H
