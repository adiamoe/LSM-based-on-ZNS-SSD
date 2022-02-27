#ifndef LSM_KV_TABLE_H
#define LSM_KV_TABLE_H

#include "SkipList.h"
#include "MurmurHash3.h"
#include "utils.h"
#include <cassert>
#include <map>

class Table {
private:
    int level;
    int num;
    uint64_t dataLength;             //数据区总长度
    uint64_t TimeAndNum[2];             //时间戳和键值对数量
    uint64_t MinMaxKey[2];               //最小最大键
    bitset<81920> BloomFilter;          //过滤器
    map<uint64_t, uint32_t> offset;      //储存对应的偏移量
    map<uint64_t, string> keyValue;      //键值对存在内存
public:
    Table(MemoryManager &pool, SkipList *memTable, int _num);

    Table(MemoryManager &pool, int _level, int _num, uint64_t timeStamp, uint64_t numPair, map<uint64_t, string> &newTable);

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

    uint64_t getTimestamp() const {return TimeAndNum[0];}
    uint64_t getNumPair() const {return TimeAndNum[1];}
    uint64_t getMaxKey() const {return MinMaxKey[1];}
    uint64_t getMinKey() const {return MinMaxKey[0];}

    string getValue(MemoryManager &pool, const uint64_t key) const;
    void traverse(MemoryManager &pool, map<uint64_t, string> &pair) const;
    void clear(MemoryManager &pool);
};


#endif //LSM_KV_TABLE_H
