#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <femu.h>
#include <kvstore.h>

#define SSD_SIZE (8ULL * 1024 * 1024 * 1024)

int main()
{
    string path = "./data";
    KVStore store(path);
    struct FemuCtrl *femu = femu_init(SSD_SIZE, true, true);
    static char buf[512 * 1024];

    srand((unsigned int)time(NULL));

    while (1) {
        size_t len = (rand() % sizeof(buf)) + 1;
        int64_t offset = rand() % SSD_SIZE;
        int op = rand() & 1;

        if (offset + len >= SSD_SIZE)
            offset -= len;

        if (op)
            femu_read(femu, buf, len, offset, NULL);
        else
            femu_write(femu, buf, len, offset, NULL);

        printf("%d %lu %ld\n", op, len, offset);
    }

    return 0;
}
