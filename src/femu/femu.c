#include <femu.h>
#include "nvme.h"
#include "ftl_config.h"

#ifdef USE_LIFETIME_FTL
#include "lftl.h"
#else
#include "ftl.h"
#endif

struct poller_args {
    struct FemuCtrl *femu;
    int index;
};

static int req_cmp_pri(pqueue_pri_t next, pqueue_pri_t curr)
{
    return next > curr;
}

static pqueue_pri_t req_get_pri(void *a)
{
    return ((NvmeRequest *)a)->expire_time;
}

static void req_set_pri(void *a, pqueue_pri_t pri)
{
    ((NvmeRequest *)a)->expire_time = pri;
}

static size_t req_get_pos(void *a)
{
    return ((NvmeRequest *)a)->pos;
}

static void req_set_pos(void *a, size_t pos)
{
    ((NvmeRequest *)a)->pos = pos;
}

static inline uint64_t alloc_req_id(struct FemuCtrl *femu)
{
    return atomic_fetch_add(&femu->req_id, 1);
}

static void *femu_poller(void *arg)
{
    struct poller_args *poller_args = (struct poller_args *)arg;
    struct FemuCtrl *femu = poller_args->femu;
    int index = poller_args->index;
    struct rte_ring *to_poller = femu->to_poller[index];
    pqueue_t *pq = femu->pqs[index];
    NvmeRequest *req;
    uint64_t now;

    while (1) {
        while (femu_ring_count(to_poller) > 0) {
            assert(femu_ring_dequeue(to_poller, (void **)&req, 1) == 1);
            pqueue_insert(pq, req);
        }

        while ((req = pqueue_peek(pq)) != NULL) {
            now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
            if (now < req->expire_time)
                break;
            req->completed = true;
            pqueue_pop(pq);
        }
    }
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

    if ((offset + len) % secsz != 0)
        end_lba++;
    assert(end_lba - start_lba > 0);

    memset(&req, 0, sizeof(NvmeRequest));
    req.id = id;
    req.slba = start_lba;
    req.nlb = end_lba - start_lba;
    req.cmd.opcode = op;
    req.stime = req.expire_time = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    req.arg = arg;
    req.completed = false;

    do {
        void *obj = &req;
        ret = femu_ring_enqueue(femu->to_ftl[poller_index], &obj, 1);
    } while (ret != 1);

    /* wait until the request finishes */
    while (!req.completed);
}

struct FemuCtrl *femu_init(size_t ssd_size, bool enable_backend, bool enable_latency)
{
    struct FemuCtrl *femu = g_malloc0(sizeof(struct FemuCtrl));
    struct poller_args args;

    femu->ssd = g_malloc0(sizeof(struct ssd));
    femu->backend = enable_backend ? g_malloc0(ssd_size) : NULL;
    femu->devsz = ssd_size;

    femu->enable_latency = enable_latency;

    femu->num_poller = FEMU_NUM_POLLER;
    femu->to_ftl = g_malloc0(FEMU_NUM_POLLER * sizeof(struct rte_ring *));
    femu->to_poller = g_malloc0(FEMU_NUM_POLLER * sizeof(struct rte_ring *));
    femu->pqs = g_malloc0(FEMU_NUM_POLLER * sizeof(pqueue_t *));
    femu->pollers = g_malloc0(FEMU_NUM_POLLER * sizeof(pthread_t));

    for (int i = 0; i < FEMU_NUM_POLLER; i++) {
        femu->to_ftl[i] = femu_ring_create(FEMU_RING_TYPE_MP_SC, FEMU_MAX_INF_REQS);
        assert(femu->to_ftl[i] != NULL);

        femu->to_poller[i] = femu_ring_create(FEMU_RING_TYPE_SP_SC, FEMU_MAX_INF_REQS);
        assert(femu->to_poller[i] != NULL);

        femu->pqs[i] = pqueue_init(FEMU_MAX_INF_REQS, req_cmp_pri, req_get_pri,
                                              req_set_pri, req_get_pos, req_set_pos);
        assert(femu->pqs[i] != NULL);

        args.femu = femu;
        args.index = i;
        assert(pthread_create(&femu->pollers[i], NULL, femu_poller, &args) == 0);
    }

    femu->ssd->dataplane_started_ptr = &femu->dataplane_started;

    ssd_init(femu);

    femu->dataplane_started = true;

    return femu;
}

int femu_read(struct FemuCtrl *femu, void *buf, size_t len, off_t offset, void *arg)
{
    if (femu->backend != NULL)
        memcpy(buf, femu->backend + offset, len);

    dispatch_io(femu, len, offset, arg, NVME_CMD_READ);

    return 0;
}

int femu_write(struct FemuCtrl *femu, const void *buf, size_t len, off_t offset, void *arg)
{
    if (femu->backend != NULL)
        memcpy(femu->backend + offset, buf, len);

    dispatch_io(femu, len, offset, arg, NVME_CMD_WRITE);

    return 0;
}
