#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <femu.h>

#define SSD_SIZE (8ULL * 1024 * 1024 * 1024)

int main()
{
    struct FemuCtrl *femu = femu_init(SSD_SIZE);
    static char buf[512 * 1024];

    srand((unsigned int)time(NULL));

    while (1) {
        size_t len = (rand() % sizeof(buf)) + 1;
        off_t offset = rand() % SSD_SIZE;
        int op = rand() & 1;

        if (offset + len >= SSD_SIZE)
            offset -= len;

        if (op)
            femu_read(femu, buf, len, offset);
        else
            femu_write(femu, buf, len, offset);

        printf("%d %lu %ld\n", op, len, offset);
    }

    return 0;
}
