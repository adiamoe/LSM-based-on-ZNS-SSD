#include <kvstore.h>

const int Max = 1024*32;

int main()
{
    KVStore store;
    int i=0;
    for (i = 0; i < Max; ++i) {
        store.put(i, std::string(i+1, 's'));
    }
    for (i = 1; i < Max; i+=4) {
        store.put(i, std::string(i+1, 't'));
    }
    for (i = 0; i < Max; i+=2)
        store.get(i);
    return 0;
}
