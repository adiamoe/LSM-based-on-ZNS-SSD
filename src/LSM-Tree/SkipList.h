#ifndef LSM_KV_SKIPLIST_H
#define LSM_KV_SKIPLIST_H

#include "MurmurHash3.h"
#include <utility>
#include <vector>
#include <string>
#include <cmath>
#include <fstream>
#include <bitset>
#include <iostream>

constexpr uint64_t filterSize = 1024 * 1024;
constexpr uint64_t InitialSize = 7*4 + filterSize;  //元信息+过滤器长度
using namespace std;

struct Node{
    Node *right, *down;
    uint64_t key;
    string val;
    Node(Node *right, Node *down, uint64_t key, string s): right(right), down(down), key(key), val(std::move(s)){}
    Node(): right(nullptr), down(nullptr) ,key(-1){}
};

class SkipList {
private:
    //储存的键值对个数
    uint64_t Size;
    //头结点
    Node *head;
    //时间戳，即SSTable序号
    uint64_t timeStamp;

    uint64_t minKey;

    uint64_t maxKey;

public:

    //转换成SSTable占用的空间大小
    size_t memory;

    SkipList():Size(0), head(nullptr), memory(InitialSize), timeStamp(0), minKey(INT64_MAX), maxKey(0){}

    //get the value of key
    string get(uint64_t key);

    //insert the KV into Skiplist
    void put(uint64_t key, const string &value);

    //清空所有元素
    void clear();

    Node* GetFirstNode();

    uint64_t getTimeStamp() { return timeStamp++; }
    uint64_t getSize() const {return Size;}
    pair<uint64_t, uint64_t> getMinMaxKey() {return {minKey, maxKey};}
};


#endif //LSM_KV_SKIPLIST_H
