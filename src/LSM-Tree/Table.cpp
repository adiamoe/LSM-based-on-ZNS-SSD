#include "Table.h"


Table::Table(MemoryManager &pool, SkipList *memTable, int order) {
    metaInfo.level = 0;
    metaInfo.order = order;
    Node *node = memTable->GetFirstNode()->right;

    metaInfo.timeStamp = memTable->getTimeStamp();
    uint64_t size = memTable->getSize();
    metaInfo.pairNum = size;
    auto min_max = memTable->getMinMaxKey();
    minMaxKey[0] = min_max.first;
    minMaxKey[1] = min_max.second;

    //写入生成对应的布隆过滤器
    uint64_t tempKey;
    const char* tempValue;
    unsigned int hash[4] = {0};
    while(node)
    {
        tempKey = node->key;
        MurmurHash3_x64_128(&tempKey, sizeof(tempKey), 1, hash);
        for(auto i:hash)
            bloomFilter.set(i%bloomFilter.size());
        node = node->right;
    }

    //索引区，计算key对应的索引值，同时储存数据
    const unsigned dataArea = InitialSize + size * 12;   //数据区开始的位置
    uint32_t index = 0;
    node = memTable->GetFirstNode()->right;
    unsigned curlength = 0;
    while(node)
    {
        tempKey = node->key;
        index = dataArea + curlength;
        offset.emplace_back(tempKey, index);
        keyValue.emplace_back(tempKey, move(node->val));
        curlength += node->val.size()+1;
        node = node->right;
    }

    metaInfo.dataLength = curlength;
    pool.writeTable(0, order);
    memTable->clear();
}

Table::Table(MemoryManager &pool, int level, int order, uint64_t timeStamp, uint64_t numPair, vector<pair<uint64_t, string>> &newTable) {
    metaInfo.level = level;
    metaInfo.order = order;

    auto iter1 = newTable.begin();
    uint64_t minKey = iter1->first;
    auto iter2 = newTable.rbegin();
    uint64_t maxKey = iter2->first;

    //写入时间戳、键值对个数和最小最大键
    metaInfo.timeStamp = timeStamp;
    metaInfo.pairNum = numPair;
    minMaxKey[0] = minKey;
    minMaxKey[1] = maxKey;

    //写入生成对应的布隆过滤器
    uint64_t tempKey;
    const char* tempValue;
    unsigned int hash[4] = {0};
    while(iter1!=newTable.end())
    {
        tempKey = iter1->first;
        MurmurHash3_x64_128(&tempKey, sizeof(tempKey), 1, hash);
        for(auto i:hash)
            bloomFilter.set(i%bloomFilter.size());
        iter1++;
    }

    //索引区，计算key对应的索引值
    const unsigned dataArea = InitialSize + numPair * 12;   //数据区开始的位置
    uint32_t index = 0;
    unsigned curlength = 0;
    iter1 = newTable.begin();
    while(iter1!=newTable.end())
    {
        tempKey = iter1->first;
        index = dataArea + curlength;
        offset.emplace_back(tempKey, index);
        curlength += (iter1->second).size()+1;
        keyValue.emplace_back(tempKey, move(iter1->second));
        iter1++;
    }

    metaInfo.dataLength = curlength;
    pool.writeTable(level, order);
}

int Table::search(uint64_t target) const{
    int low = 0, high = offset.size() - 1;
    while(low <= high){
        int mid = (high - low) / 2 + low;
        uint64_t num = offset[mid].first;
        if (num == target) {
            return mid;
        } else if (num > target) {
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }
    return -1;
}

/**
 * 根据输入的key，寻找文件中有无对应的value
 */
string Table::getValue(MemoryManager &pool, uint64_t key) const{
    if(key<minMaxKey[0] || key>minMaxKey[1])
        return "";
    //通过布隆过滤器判断key是否存在，如果有其中一个bit为0，则证明不存在
    unsigned int hash[4] = {0};
    MurmurHash3_x64_128(&key, sizeof(key), 1, hash);
    for(unsigned int i : hash)
        if(!bloomFilter.test(i%bloomFilter.size()))
            return "";

    //再在键值对中进行查找
    if(search(key) == -1)
        return "";

    int index1 = search(key);
    int index2 = index1;
    index2++;

    const unsigned dataArea = InitialSize + metaInfo.pairNum * 12;
    uint64_t len = (index2 != offset.size())? (offset[index2].second-offset[index1].second):(dataArea + metaInfo.dataLength - offset[index1].second);

    pool.getValue(metaInfo.level, metaInfo.order, offset[index1].second, len);

    string ans = keyValue[search(key)].second;
    return ans;
}

//遍历文件，将键值对全部读进内存
void Table::traverse(MemoryManager &pool, vector<pair<uint64_t, string>> &pair) const{
    pair.reserve(1024);
    pair = keyValue;
    const unsigned dataArea = InitialSize + metaInfo.pairNum * 12;
    pool.readTable(metaInfo.level, metaInfo.order, dataArea, metaInfo.dataLength);
}

void Table::clear(MemoryManager &pool) const {
    pool.deleteTable(metaInfo.level, metaInfo.order);
}