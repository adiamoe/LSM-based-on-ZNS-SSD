#include "SkipList.h"

string SkipList::get(uint64_t key)
{
    if(Size == 0)
        return "";
    Node *p = head;
    while(p)
    {
        while(p->right && p->right->key<key)
        {
            p = p->right;
        }
        if(p->right && p->right->key == key)
            return (p->right->val);
        p = p->down;
    }
    return "";
}

void SkipList::put(uint64_t key, const string &value)
{
    //更新最大最小值键
    if(key<minKey)
        minKey = key;
    if(key>maxKey)
        maxKey = key;

    vector<Node*> pathList;    //从上至下记录搜索路径
    Node *p = head;
    while(p){
        while(p->right && p->right->key <= key){
            p = p->right;
        }
        pathList.push_back(p);
        p = p->down;
    }

    //对于相同的key，MemTable中进行替换
    if(!pathList.empty() && pathList.back()->key == key)
    {
        while(!pathList.empty() && pathList.back()->key == key)
        {
            Node *node = pathList.back();
            pathList.pop_back();
            node->val = value;
        }
        return;
    }

    //如果不存在相同的key，则进行插入
    bool insertUp = true;
    Node* downNode= nullptr;
    Size++;
    while(insertUp && !pathList.empty()){   //从下至上搜索路径回溯，50%概率
        Node *insert = pathList.back();
        pathList.pop_back();
        insert->right = new Node(insert->right, downNode, key, value); //add新结点
        downNode = insert->right;    //把新结点赋值为downNode
        insertUp = (rand()&1);   //50%概率
    }
    if(insertUp){  //插入新的头结点，加层
            Node* oldHead = head;
            head = new Node();
            head->right = new Node(nullptr, downNode, key, value);
            head->down = oldHead;
    }
}

Node* SkipList::GetFirstNode()
{
    Node *p = head, *q = nullptr;
    while(p){
        q = p;
        p = p->down;
    }
    return q;
}

void SkipList::clear()
{
    if(!head)
        return;
    //释放掉所有数据，防止内存泄漏
    Node *p = head, *q = nullptr, *down = nullptr;
    do
    {
        down = p->down;
        do{
            q = p->right;
            delete p;
            p = q;
        }while(p);
        p = down;
    }while(p);

    //head指针置空，重置除时间戳外的其它数据项
    head = nullptr;
    Size = 0;
    memory = 10272;
    minKey = INT64_MAX;
    maxKey = INT64_MIN;
}