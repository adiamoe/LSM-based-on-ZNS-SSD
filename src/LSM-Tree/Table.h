#include "SkipList.h"
#include "MurmurHash3.h"
#include <assert.h>
#include <map>
#ifndef LSM_KV_TABLE_H
#define LSM_KV_TABLE_H


class Table {
private:
    string sstable;                     //文件路径及文件名
    uint64_t TimeAndNum[2];             //时间戳和键值对数量
    int64_t MinMaxKey[2];               //最小最大键
    bitset<81920> BloomFilter;          //过滤器
    map<int64_t, uint32_t> offset;      //储存对应的偏移量
public:
    Table() {sstable = "";}
    Table(string &fileName);

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

    string getFileName() const {return sstable;}
    uint64_t getTimestamp() const {return TimeAndNum[0];}
    uint64_t getNumPair() const {return TimeAndNum[1];}
    int64_t getMaxKey() const {return MinMaxKey[1];}
    int64_t getMinKey() const {return MinMaxKey[0];}

    string getValue(uint64_t key) const;
    void open();
    void traverse(map<int64_t, string> &pair) const;
    void reset() const;
};


#endif //LSM_KV_TABLE_H
