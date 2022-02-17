#ifndef __FEMU_NVME_H
#define __FEMU_NVME_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "pqueue.h"

#define FEMU_NUM_POLLER 1

enum NvmeIoCommands {
    NVME_CMD_FLUSH              = 0x00,
    NVME_CMD_WRITE              = 0x01,
    NVME_CMD_READ               = 0x02,
    NVME_CMD_WRITE_UNCOR        = 0x04,
    NVME_CMD_COMPARE            = 0x05,
    NVME_CMD_WRITE_ZEROES       = 0x08,
    NVME_CMD_DSM                = 0x09,
    NVME_CMD_ZONE_MGMT_SEND     = 0x79,
    NVME_CMD_ZONE_MGMT_RECV     = 0x7a,
    NVME_CMD_ZONE_APPEND        = 0x7d,
    NVME_CMD_OC_ERASE           = 0x90,
    NVME_CMD_OC_WRITE           = 0x91,
    NVME_CMD_OC_READ            = 0x92,
};

typedef struct NvmeCmd {
    uint8_t    opcode;
} NvmeCmd;

typedef struct NvmeRequest {
    uint64_t                id;
    uint64_t                slba;
    uint16_t                nlb;
    NvmeCmd                 cmd;
    int64_t                 stime;
    int64_t                 reqlat;
    int64_t                 expire_time;

    /* position in the priority queue for delay emulation */
    size_t                  pos;

    bool completed;
} NvmeRequest;

typedef struct FemuCtrl {
    struct ssd           *ssd;
    void                 *backend;
    uint64_t             devsz;

    bool                 enable_latency;

    uint32_t             num_poller;
    struct rte_ring      **to_ftl;
    struct rte_ring      **to_poller;
    pqueue_t             **pqs;
    pthread_t            *pollers;

    bool                 dataplane_started;

    atomic_uint_fast64_t req_id;
} FemuCtrl;

typedef struct NvmePollerThreadArgument {
    FemuCtrl        *n;
    int             index;
} NvmePollerThreadArgument;

typedef struct NvmeDifTuple {
    uint16_t guard_tag;
    uint16_t app_tag;
    uint32_t ref_tag;
} NvmeDifTuple;

typedef enum {
    QEMU_CLOCK_REALTIME = 0,
    QEMU_CLOCK_MAX
} QEMUClockType;

static inline int64_t qemu_clock_get_ns(QEMUClockType type)
{
    struct timespec ts;

    assert(type == QEMU_CLOCK_REALTIME);

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static inline void *g_malloc0(size_t size)
{
    void *ret = malloc(size);

    assert(ret != NULL);

    memset(ret, 0, size);

    return ret;
}

//#define FEMU_DEBUG_NVME
#ifdef FEMU_DEBUG_NVME
#define femu_debug(fmt, ...) \
    do { printf("[FEMU] Dbg: " fmt, ## __VA_ARGS__); } while (0)
#else
#define femu_debug(fmt, ...) \
    do { } while (0)
#endif

#define femu_err(fmt, ...) \
    do { fprintf(stderr, "[FEMU] Err: " fmt, ## __VA_ARGS__); } while (0)

#define femu_log(fmt, ...) \
    do { printf("[FEMU] Log: " fmt, ## __VA_ARGS__); } while (0)


#endif /* __FEMU_NVME_H */
