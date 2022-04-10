#ifndef LSM_KV_TABLE_H
#define LSM_KV_TABLE_H

#include "SkipList.h"
#include "MurmurHash3.h"
#include "utils.h"
#include <cassert>
#include <map>

struct Meta {
    uint32_t timeStamp;         //时间戳
    uint32_t level;             //层数
    uint32_t order;             //层内序号
    uint32_t dataLength;        //数据总长度
    uint32_t pairNum;           //键值对数量
};

class Table {
private:
    Meta metaInfo{};
    uint64_t minMaxKey[2]{};               //最小最大键
    bitset<81920> bloomFilter;          //过滤器
    vector<pair<uint64_t, uint32_t>> offset;      //储存对应的偏移量
    vector<pair<uint64_t, string>> keyValue;      //键值对存在内存
public:
    Table(MemoryManager &pool, SkipList *memTable, int order);

    Table(MemoryManager &pool, int level, int order, uint64_t timeStamp, uint64_t numPair, vector<pair<uint64_t, string>> &newTable);

    //时间戳小的排前面
    bool operator<(const Table& temp) const
    {
        if(getTimestamp()==temp.getTimestamp())
            return getMinKey()<temp.getMinKey();
        return getTimestamp()<temp.getTimestamp();
    }

    bool operator>(const Table& temp) const
    {
        if(getTimestamp()==temp.getTimestamp())
            return getMinKey()>temp.getMinKey();
        return getTimestamp()>temp.getTimestamp();
    }

    uint64_t getTimestamp() const {return metaInfo.timeStamp;}
    uint64_t getNumPair() const {return metaInfo.pairNum;}
    uint64_t getMaxKey() const {return minMaxKey[1];}
    uint64_t getMinKey() const {return minMaxKey[0];}

    string getValue(MemoryManager &pool, uint64_t key) const;
    void traverse(MemoryManager &pool, vector<pair<uint64_t, string>> &pair) const;
    void clear(MemoryManager &pool) const;
    int search(uint64_t target) const;
};


#endif //LSM_KV_TABLE_H
