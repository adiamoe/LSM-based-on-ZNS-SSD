#ifndef __FTL_CONFIG_H
#define __FTL_CONFIG_H

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

#endif
