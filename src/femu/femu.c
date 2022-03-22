#include <femu.h>
#include "nvme.h"
#include "ftl_config.h"
#include "ftl.h"

static inline uint64_t alloc_req_id(struct FemuCtrl *femu)
{
    return atomic_fetch_add(&femu->req_id, 1);
}

static void dispatch_io(struct FemuCtrl *femu, size_t len, off_t offset, void *arg, int op)
{
    uint64_t id = alloc_req_id(femu);
    uint64_t secsz = femu->ssd->sp.secsz;
    uint64_t start_lba = offset / secsz;
    uint64_t end_lba = (offset + len) / secsz;
    int poller_index = 0; /* need to support multiple pollers in the future */
    NvmeRequest req;
    int ret;

    memset(&req, 0, sizeof(NvmeRequest));
    req.id = id;
    req.slba = start_lba;
    req.nlb = end_lba - start_lba;
    req.cmd.opcode = op;
    req.stime = req.expire_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    req.arg = arg;
    req.completed = false;

    uint64_t lat;
    switch (req.cmd.opcode) {
        case NVME_CMD_WRITE:
            lat = zns_write(femu, &req);
            break;
        case NVME_CMD_READ:
            lat = zns_read(femu, &req);
            break;
        case NVME_CMD_RESET:
            zns_reset(femu, &req);
            lat = 0;
            break;
        default:
            ftl_err("FTL received unkown request type, ERROR\n");
    }

    if (femu->enable_latency) {
        req.reqlat = lat;
        req.expire_time += lat;
    }

    uint64_t now;
    while (1) {
        now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        if (now >= req.expire_time)
            break;
    }
}

struct FemuCtrl *femu_init(size_t ssd_size, bool enable_backend, bool enable_latency)
{
    struct FemuCtrl *femu = g_malloc0(sizeof(struct FemuCtrl));

    femu->ssd = g_malloc0(sizeof(struct ssd));
    femu->backend = enable_backend ? g_malloc0(ssd_size) : NULL;
    femu->devsz = ssd_size;

    femu->enable_latency = enable_latency;

    femu->ssd->dataplane_started_ptr = &femu->dataplane_started;

    ssd_init(femu);
    zns_init(femu);

    femu->dataplane_started = true;

    return femu;
}

int femu_read(struct FemuCtrl *femu, size_t len, off_t offset, void *arg)
{
    dispatch_io(femu, len, offset, arg, NVME_CMD_READ);

    return 0;
}

int femu_write(struct FemuCtrl *femu, size_t len, off_t offset, void *arg)
{
    dispatch_io(femu, len, offset, arg, NVME_CMD_WRITE);

    return 0;
}

int femu_reset(struct FemuCtrl *femu, size_t len, off_t offset, void *arg) {

    dispatch_io(femu, len, offset, arg, NVME_CMD_RESET);

    return 0;
}

void get_zns_meta(uint64_t *meta) {
    meta[0] = ZONE_SIZE;
    meta[1] = NUM_FCGS;
    uint64_t block_size = SSD_SECSZ * SSD_SECS_PER_PG * SSD_PGS_PER_BLK;
    meta[2] = ZONE_SIZE / block_size / (SSD_NCHS / NUM_FCGS);   //每个zone在每个chip上占据的block数
    meta[3] = SSD_BLKS_PER_PL / meta[2];                        //每个die可以容纳的zone数量
}
