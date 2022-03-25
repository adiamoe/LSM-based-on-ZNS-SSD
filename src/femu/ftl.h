#ifndef __FEMU_FTL_H
#define __FEMU_FTL_H

#include "ftl_config.h"

#ifndef USE_LIFETIME_FTL

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include "nvme.h"

enum {
    NAND_READ =  0,
    NAND_WRITE = 1,
    NAND_ERASE = 2,

    NAND_READ_LATENCY = 40,
    NAND_PROG_LATENCY = 200,
    NAND_ERASE_LATENCY = 20000,
};

enum {
    USER_IO = 0,
    GC_IO = 1,
};

/* describe a physical page addr */
struct ppa {
    uint64_t blk;
    uint64_t pg;
    uint64_t pl;
    uint64_t lun;
    uint64_t ch;
};

struct nand_block {
    int npgs;
    int erase_cnt;
};

struct nand_plane {
    struct nand_block *blk;
    int nblks;
};

struct nand_lun {
    struct nand_plane *pl;
    int npls;
    uint64_t next_lun_avail_time;
    bool busy;
    uint64_t gc_endtime;
};

struct ssd_channel {
    struct nand_lun *lun;
    int nluns;
    uint64_t next_ch_avail_time;
    bool busy;
    uint64_t gc_endtime;
};

struct ssdparams {
    int secsz;        /* sector size in bytes */
    int secs_per_pg;  /* # of sectors per page */
    int pgs_per_blk;  /* # of NAND pages per block */
    int blks_per_pl;  /* # of blocks per plane */
    int pls_per_lun;  /* # of planes per LUN (Die) */
    int luns_per_ch;  /* # of LUNs per channel */
    int nchs;         /* # of channels in the SSD */

    int pg_rd_lat;    /* NAND page read latency in nanoseconds */
    int pg_wr_lat;    /* NAND page program latency in nanoseconds */
    int blk_er_lat;   /* NAND block erase latency in nanoseconds */
    int ch_xfer_lat;  /* channel transfer latency for one page in nanoseconds
                       * this defines the channel bandwith
                       */

    double gc_thres_pcent;
    int gc_thres_lines;
    double gc_thres_pcent_high;
    int gc_thres_lines_high;
    bool enable_gc_delay;

    /* below are all calculated values */
    int secs_per_blk; /* # of sectors per block */
    int secs_per_pl;  /* # of sectors per plane */
    int secs_per_lun; /* # of sectors per LUN */
    int secs_per_ch;  /* # of sectors per channel */
    int tt_secs;      /* # of sectors in the SSD */

    int pgs_per_pl;   /* # of pages per plane */
    int pgs_per_lun;  /* # of pages per LUN (Die) */
    int pgs_per_ch;   /* # of pages per channel */
    int tt_pgs;       /* total # of pages in the SSD */

    int blks_per_lun; /* # of blocks per LUN */
    int blks_per_ch;  /* # of blocks per channel */
    int tt_blks;      /* total # of blocks in the SSD */

    int secs_per_line;
    int pgs_per_line;
    int blks_per_line;
    int tt_lines;

    int pls_per_ch;   /* # of planes per channel */
    int tt_pls;       /* total # of planes in the SSD */

    int tt_luns;      /* total # of LUNs in the SSD */

    uint64_t device_size; /* size exposed to host, in bytes */
    uint64_t flash_size;  /* size of NAND flash, in bytes */
};

struct zns_write_pointer {
    int FCGid;
    uint64_t ch;
    uint64_t lun;
    uint64_t pg;
    uint64_t blk;
    uint64_t pl;
};

struct nand_cmd {
    int type;
    int cmd;
    int64_t stime; /* Coperd: request arrival time */
};

struct ssd {
    char *ssdname;
    struct ssdparams sp;
    struct ssd_channel *ch;

    bool *dataplane_started_ptr;
};

void ssd_init(FemuCtrl *n);

void zns_init(FemuCtrl *n);

uint64_t zns_write(FemuCtrl *n, NvmeRequest *req);

uint64_t zns_read(FemuCtrl *n, NvmeRequest *req);

void zns_reset(FemuCtrl *n, NvmeRequest *req);

typedef enum NvmeZoneState {
    NVME_ZONE_STATE_EMPTY            ,
    NVME_ZONE_STATE_OPEN             ,
    NVME_ZONE_STATE_FULL             ,
} NvmeZoneState;

typedef struct NvmeZoneDescr {
    NvmeZoneState   state;
    uint64_t        start_lba;
    uint64_t        zone_cap;
} NvmeZoneDescr;

typedef struct NvmeZone {
    int                     ZoneID;
    NvmeZoneDescr           d;
    uint64_t                write_ptr;
    struct zns_write_pointer    wpp;
} NvmeZone;

#ifdef FEMU_DEBUG_FTL
#define ftl_debug(fmt, ...) \
    do { printf("[FEMU] FTL-Dbg: " fmt, ## __VA_ARGS__); } while (0)
#else
#define ftl_debug(fmt, ...) \
    do { } while (0)
#endif

#define ftl_err(fmt, ...) \
    do { fprintf(stderr, "[FEMU] FTL-Err: " fmt, ## __VA_ARGS__); } while (0)

#define ftl_log(fmt, ...) \
    do { printf("[FEMU] FTL-Log: " fmt, ## __VA_ARGS__); } while (0)


/* FEMU assert() */
#ifdef FEMU_DEBUG_FTL
#define ftl_assert(expression) assert(expression)
#else
#define ftl_assert(expression)
#endif

#endif /* USE_LIFETIME_FTL */

#endif
