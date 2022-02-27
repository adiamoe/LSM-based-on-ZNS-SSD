#include "Table.h"

Table::Table(MemoryManager &pool, SkipList *memTable, int _num) {
    level = 0;
    num = _num;
    Node *node = memTable->GetFirstNode()->right;

    TimeAndNum[0] = memTable->getTimeStamp();
    uint64_t size = memTable->getSize();
    TimeAndNum[1] = size;
    auto min_max = memTable->getMinMaxKey();
    MinMaxKey[0] = min_max.first;
    MinMaxKey[1] = min_max.second;

    //写入生成对应的布隆过滤器
    uint64_t tempKey;
    const char* tempValue;
    unsigned int hash[4] = {0};
    while(node)
    {
        tempKey = node->key;
        MurmurHash3_x64_128(&tempKey, sizeof(tempKey), 1, hash);
        for(auto i:hash)
            BloomFilter.set(i%81920);
        node = node->right;
    }

    //索引区，计算key对应的索引值，同时储存数据
    const int dataArea = InitialSize + size * 12;   //数据区开始的位置
    uint32_t index = 0;
    node = memTable->GetFirstNode()->right;
    int curlength = 0;
    while(node)
    {
        tempKey = node->key;
        index = dataArea + curlength;
        offset[tempKey] = index;
        keyValue[tempKey] = node->val;
        curlength += node->val.size()+1;
        node = node->right;
    }

    dataLength = curlength;
    pool.writeTable(0, num);
    memTable->clear();
}

Table::Table(MemoryManager &pool, int _level, int _num, uint64_t timeStamp, uint64_t numPair, map<uint64_t, string> &newTable) {
    level = _level;
    num = _num;

    auto iter1 = newTable.begin();
    uint64_t minKey = iter1->first;
    auto iter2 = newTable.rbegin();
    uint64_t maxKey = iter2->first;

    //写入时间戳、键值对个数和最小最大键
    TimeAndNum[0] = timeStamp;
    TimeAndNum[1] = numPair;
    MinMaxKey[0] = minKey;
    MinMaxKey[1] = maxKey;

    //写入生成对应的布隆过滤器
    uint64_t tempKey;
    const char* tempValue;
    unsigned int hash[4] = {0};
    while(iter1!=newTable.end())
    {
        tempKey = iter1->first;
        MurmurHash3_x64_128(&tempKey, sizeof(tempKey), 1, hash);
        for(auto i:hash)
            BloomFilter.set(i%81920);
        iter1++;
    }

    //索引区，计算key对应的索引值
    const int dataArea = InitialSize + numPair * 12;   //数据区开始的位置
    uint32_t index = 0;
    int curlength = 0;
    iter1 = newTable.begin();
    while(iter1!=newTable.end())
    {
        tempKey = iter1->first;
        index = dataArea + curlength;
        offset[tempKey] = index;
        keyValue[tempKey] = iter1->second;
        curlength += (iter1->second).size()+1;
        iter1++;
    }

    dataLength = curlength;
    pool.writeTable(level, num);
}

/**
 * 根据输入的key，寻找文件中有无对应的value
 */
string Table::getValue(MemoryManager &pool, const uint64_t key) const{
    if(key<MinMaxKey[0] || key>MinMaxKey[1])
        return "";
    //通过布隆过滤器判断key是否存在，如果有其中一个bit为0，则证明不存在
    unsigned int hash[4] = {0};
    MurmurHash3_x64_128(&key, sizeof(key), 1, hash);
    for(unsigned int i : hash)
        if(!BloomFilter[i%81920])
            return "";

    //再在键值对中进行查找
    if(offset.count(key)==0)
        return "";
    auto iter1 = offset.find(key);
    auto iter2 = offset.find(key);
    iter2++;

    const int dataArea = InitialSize + TimeAndNum[1] * 12;
    uint64_t len = iter2!=offset.end()? (iter2->second-iter1->second):(dataArea + dataLength - iter1->second);

    pool.readTable(level, num, iter1->second, len);

    string ans = keyValue.find(key)->second;
    return ans;
}

//遍历文件，将键值对全部读进内存
void Table::traverse(MemoryManager &pool, map<uint64_t, string> &pair) const{
    pair = keyValue;
    const int dataArea = InitialSize + TimeAndNum[1] * 12;
    pool.readTable(level, num, dataArea, dataLength);
}

void Table::clear(MemoryManager &pool) {
    pool.deleteTable(level, num);
}