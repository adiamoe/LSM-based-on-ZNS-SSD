#ifndef __FEMU_FTL_H
#define __FEMU_FTL_H

#include "ftl_config.h"

#ifdef USE_LIFETIME_FTL

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include "queue.h"
#include "pqueue.h"
#include "rte_ring.h"
#include "nvme.h"

#define INVALID_PPA     (~(0ULL))
#define INVALID_LPN     (~(0ULL))
#define UNMAPPED_PPA    (~(0ULL))

#define WB_INDEX_NULL   (-1)

/* hot classes first */
static double lifetime_class_proportions[] = {
    0.1,
    0.9,
};

#define NR_LIFETIME_CLASS (sizeof(lifetime_class_proportions) / sizeof(lifetime_class_proportions[0]))

#define LIFETIME_CLASS_UNSEEN NR_LIFETIME_CLASS

enum {
    NAND_READ =  0,
    NAND_WRITE = 1,
    NAND_ERASE = 2,

    NAND_READ_LATENCY = 40000,
    NAND_PROG_LATENCY = 200000,
    NAND_ERASE_LATENCY = 2000000,
};

enum {
    USER_IO = 0,
    GC_IO = 1,
};

enum {
    SEC_FREE = 0,
    SEC_INVALID = 1,
    SEC_VALID = 2,

    PG_FREE = 0,
    PG_INVALID = 1,
    PG_VALID = 2
};

enum {
    FEMU_ENABLE_GC_DELAY = 1,
    FEMU_DISABLE_GC_DELAY = 2,

    FEMU_ENABLE_DELAY_EMU = 3,
    FEMU_DISABLE_DELAY_EMU = 4,

    FEMU_RESET_ACCT = 5,
    FEMU_ENABLE_LOG = 6,
    FEMU_DISABLE_LOG = 7,
};


#define BLK_BITS    (16)
#define PG_BITS     (16)
#define SEC_BITS    (8)
#define PL_BITS     (8)
#define LUN_BITS    (8)
#define CH_BITS     (7)

/* describe a physical page addr */
struct ppa {
    union {
        struct {
            uint64_t blk : BLK_BITS;
            uint64_t pg  : PG_BITS;
            uint64_t sec : SEC_BITS;
            uint64_t pl  : PL_BITS;
            uint64_t lun : LUN_BITS;
            uint64_t ch  : CH_BITS;
            uint64_t rsv : 1;
        } g;

        uint64_t ppa;
    };
};

typedef int nand_sec_status_t;

struct nand_page {
    nand_sec_status_t *sec;
    int nsecs;
    int status;
};

struct nand_block {
    struct nand_page *pg;
    int npgs;
    int ipc; /* invalid page count */
    int vpc; /* valid page count */
    int erase_cnt;
    int wp; /* current write pointer */

    QTAILQ_ENTRY(nand_block) tq;

    size_t pos;
    pqueue_pri_t pri; /* vpc */
};

struct nand_plane {
    struct nand_block *blk;
    int nblks;
    int writable_blk_cnt; /* includes active blocks */

    struct nand_block *active_blks[NR_LIFETIME_CLASS];
    struct nand_block *unseen;
    QTAILQ_HEAD(free_blks, nand_block) free_blks;
    QTAILQ_HEAD(full_blks, nand_block) full_blks;
    pqueue_t *victim_blks;
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

    uint64_t write_buffer_size;

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

/* wp: record next write addr */
struct write_pointer {
    int ch;
    int lun;
    int pl;
    struct nand_block *last_written_blk;
};

struct nand_cmd {
    int type;
    int cmd;
    int64_t stime; /* Coperd: request arrival time */
};

struct logical_page {
    uint64_t lpn;
    int lc;
    QTAILQ_ENTRY(logical_page) lru;
};

struct lifetime_mgmt {
    struct logical_page *pgs;
    QTAILQ_HEAD(classes, logical_page) classes[NR_LIFETIME_CLASS];
    uint64_t pg_cnts[NR_LIFETIME_CLASS];
    uint64_t max_pgs[NR_LIFETIME_CLASS];
};

struct gc_info {
    uint64_t writable_blk_cnt; /* includes active blocks in each plane */

    uint64_t global_gc_thres;
    uint64_t per_pl_gc_thres;

    bool gc_requested;
};

struct write_buffer_slot {
    uint64_t lpn;
    bool dirty;
    QTAILQ_ENTRY(write_buffer_slot) lru;
};

struct write_buffer {
    struct write_buffer_slot *slots;

    QTAILQ_HEAD(free_slots, write_buffer_slot) free_slots;
    QTAILQ_HEAD(used_slots, write_buffer_slot) used_slots;

    int *lpn2index;

    uint64_t nslots;
    uint64_t used_slot_cnt;
    uint64_t dirty_slot_cnt;
};

struct ssd {
    char *ssdname;
    struct ssdparams sp;
    struct ssd_channel *ch;
    struct ppa *maptbl; /* page level mapping table */
    uint64_t *rmap;     /* reverse mapptbl, assume it's stored in OOB */
    struct write_pointer wp;
    struct write_buffer wb;

    /* lockless ring for communication with NVMe IO thread */
    struct rte_ring **to_ftl;
    struct rte_ring **to_poller;
    bool *dataplane_started_ptr;
    pthread_t ftl_thread;

    struct lifetime_mgmt lifetime_mgmt;
    struct gc_info gc_info;
};

void ssd_init(FemuCtrl *n);

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

#define FOR_EACH_CHANNEL(ssdp, chp) \
for ((chp) = (ssdp)->ch; (chp) != (ssdp)->ch + (ssdp)->sp.nchs; (chp)++)

#define FOR_EACH_LUN(ssdp, chp, lunp) \
FOR_EACH_CHANNEL((ssdp), (chp)) \
    for ((lunp) = (chp)->lun; (lunp) != (chp)->lun + (chp)->nluns; (lunp)++)

#define FOR_EACH_PLANE(ssdp, chp, lunp, plp) \
FOR_EACH_LUN((ssdp), (chp), (lunp)) \
    for((plp) = (lunp)->pl; (plp) != (lunp)->pl + (lunp)->npls; (plp)++)

#define FOR_EACH_BLOCK(ssdp, chp, lunp, plp, blkp) \
FOR_EACH_PLANE((ssdp), (chp), (lunp), (plp)) \
    for ((blkp) = (plp)->blk; (blkp) != (plp)->blk + (plp)->nblks; (blkp)++)

#define FOR_EACH_PAGE(ssdp, chp, lunp, plp, blkp, pgp) \
FOR_EACH_BLOCK((ssdp), (chp), (lunp), (plp), (blkp)) \
    for ((pgp) = (blk)->pg; (pgp) != (blk)->pg + (blk)->npgs; (pgp)++)

#endif /* USE_LIFETIME_FTL */

#endif
