#ifndef __ACTIVE_FTL_H
#define __ACTIVE_FTL_H

//#define USE_LIFETIME_FTL

/* default: 8GB, 6.25% OP */
#define SSD_SECSZ       512
#define SSD_SECS_PER_PG 8
#define SSD_PGS_PER_BLK 256
#define SSD_BLKS_PER_PL 136
#define SSD_PLS_PER_LUN 1
#define SSD_LUNS_PER_CH 8
#define SSD_NCHS        8

/* 500GB, 8% OP */
// #define SSD_SECSZ       512
// #define SSD_SECS_PER_PG 32
// #define SSD_PGS_PER_BLK 1024
// #define SSD_BLKS_PER_PL 540
// #define SSD_PLS_PER_LUN 1
// #define SSD_LUNS_PER_CH 8
// #define SSD_NCHS        8

/* 1GB */
#define SSD_WRITE_BUFFER_SIZE (1ULL * 1024 * 1024 * 1024)

#endif
