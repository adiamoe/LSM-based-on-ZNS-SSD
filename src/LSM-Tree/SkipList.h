#ifndef LSM_KV_SKIPLIST_H
#define LSM_KV_SKIPLIST_H

#include "kvstore_api.h"
#include "MurmurHash3.h"
#include <vector>
#include <string>
#include <cmath>
#include <fstream>
#include <bitset>
#include <iostream>

const int MEMTABLE = (int) pow(2, 21);
const int InitialSize = 10272;
using namespace std;

struct Node{
    Node *right, *down;
    int64_t key;
    string val;
    Node(Node *right, Node *down, int64_t key, const string &s): right(right), down(down), key(key), val(s){}
    Node(): right(nullptr), down(nullptr) ,key(INT64_MIN), val(""){}
};

class SkipList {
private:
    //储存的键值对个数
    uint64_t Size;
    //头结点
    Node *head;
    //时间戳，即SSTable序号
    uint64_t timeStamp;

    int64_t minKey;

    int64_t maxKey;

public:

    //转换成SSTable占用的空间大小
    size_t memory;

    SkipList():Size(0), head(nullptr), memory(InitialSize), timeStamp(0), minKey(INT64_MAX), maxKey(INT64_MIN){}

    //get the value of key
    string get(int64_t key);

    //insert the KV into Skiplist
    void put(int64_t key, const string &value);

    //remove the KV pair
    //bool remove(int64_t key);

    //清空所有元素
    void clear();

    void store(int num, const std::string &dir);   //将memtable储存为SSTable

    Node* GetFirstNode();
};


#endif //LSM_KV_SKIPLIST_H
