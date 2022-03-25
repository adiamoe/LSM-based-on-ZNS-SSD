#ifndef __ACTIVE_FTL_H
#define __ACTIVE_FTL_H

/* default: 8GB, 6.25% OP*/
//#define SSD_SECSZ       512
//#define SSD_SECS_PER_PG 8
//#define SSD_PGS_PER_BLK 256
//#define SSD_BLKS_PER_PL 128
//#define SSD_PLS_PER_LUN 1
//#define SSD_LUNS_PER_CH 8
//#define SSD_NCHS        8
//#define ZONE_SIZE 1024 * 1024 * 64

/* 512GB */
#define SSD_SECSZ       512
#define SSD_SECS_PER_PG 8
#define SSD_PGS_PER_BLK 2048
#define SSD_BLKS_PER_PL 1024
#define SSD_PLS_PER_LUN 1
#define SSD_LUNS_PER_CH 8
#define SSD_NCHS        8
#define ZONE_SIZE 1024 * 1024 * 256

#define NUM_FCGS 2

#endif
