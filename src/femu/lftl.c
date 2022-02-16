#define FEMU_DEBUG_FTL

#include "lftl.h"

#ifdef USE_LIFETIME_FTL

#define ARRAY_INDEX(arr, elm) ((elm) - (arr))

static void *ftl_thread(void *arg);

static inline bool should_gc_background(struct ssd *ssd)
{
    struct gc_info *gi = &ssd->gc_info;

    return gi->gc_requested || gi->writable_blk_cnt < gi->global_gc_thres;
}

static inline bool should_gc_foreground(struct ssd *ssd)
{
    struct gc_info *gi = &ssd->gc_info;

    return gi->gc_requested;
}

static inline struct ssd_channel *get_ch(struct ssd *ssd, struct ppa *ppa)
{
    return &(ssd->ch[ppa->g.ch]);
}

static inline struct nand_lun *get_lun(struct ssd *ssd, struct ppa *ppa)
{
    struct ssd_channel *ch = get_ch(ssd, ppa);
    return &(ch->lun[ppa->g.lun]);
}

static inline struct nand_plane *get_pl(struct ssd *ssd, struct ppa *ppa)
{
    struct nand_lun *lun = get_lun(ssd, ppa);
    return &(lun->pl[ppa->g.pl]);
}

static inline struct nand_block *get_blk(struct ssd *ssd, struct ppa *ppa)
{
    struct nand_plane *pl = get_pl(ssd, ppa);
    return &(pl->blk[ppa->g.blk]);
}

static inline struct nand_page *get_pg(struct ssd *ssd, struct ppa *ppa)
{
    struct nand_block *blk = get_blk(ssd, ppa);
    return &(blk->pg[ppa->g.pg]);
}

static inline struct ppa get_maptbl_ent(struct ssd *ssd, uint64_t lpn)
{
    return ssd->maptbl[lpn];
}

static inline void set_maptbl_ent(struct ssd *ssd, uint64_t lpn, struct ppa *ppa)
{
    ftl_assert(lpn < ssd->sp.tt_pgs);
    ssd->maptbl[lpn] = *ppa;
}

static uint64_t ppa2pgidx(struct ssd *ssd, struct ppa *ppa)
{
    struct ssdparams *spp = &ssd->sp;
    uint64_t pgidx;

    pgidx = ppa->g.ch  * spp->pgs_per_ch  + \
            ppa->g.lun * spp->pgs_per_lun + \
            ppa->g.pl  * spp->pgs_per_pl  + \
            ppa->g.blk * spp->pgs_per_blk + \
            ppa->g.pg;

    ftl_assert(pgidx < spp->tt_pgs);

    return pgidx;
}

static void set_ppa_from_pointers(struct ppa *ppa, struct ssd *ssd, struct ssd_channel *ch,
        struct nand_lun *lun, struct nand_plane *pl, struct nand_block *blk,
        struct nand_page *pg)
{
    ppa->ppa = 0;

    if (pg != NULL) {
        ftl_assert(blk != NULL);
        ppa->g.pg = ARRAY_INDEX(blk->pg, pg);
    }

    if (blk != NULL) {
        ftl_assert(pl != NULL);
        ppa->g.blk = ARRAY_INDEX(pl->blk, blk);
    }

    if (pl != NULL) {
        ftl_assert(lun != NULL);
        ppa->g.pl = ARRAY_INDEX(lun->pl, pl);
    }

    if (lun != NULL) {
        ftl_assert(ch != NULL);
        ppa->g.lun = ARRAY_INDEX(ch->lun, lun);
    }

    if (ch != NULL) {
        ftl_assert(ssd != NULL);
        ppa->g.ch = ARRAY_INDEX(ssd->ch, ch);
    }
}

static inline uint64_t get_rmap_ent(struct ssd *ssd, struct ppa *ppa)
{
    uint64_t pgidx = ppa2pgidx(ssd, ppa);

    return ssd->rmap[pgidx];
}

/* set rmap[page_no(ppa)] -> lpn */
static inline void set_rmap_ent(struct ssd *ssd, uint64_t lpn, struct ppa *ppa)
{
    uint64_t pgidx = ppa2pgidx(ssd, ppa);

    ssd->rmap[pgidx] = lpn;
}

static inline int victim_line_cmp_pri(pqueue_pri_t next, pqueue_pri_t curr)
{
    return (next > curr);
}

static void ssd_init_write_pointer(struct ssd *ssd)
{
    struct write_pointer *wpp = &ssd->wp;

    wpp->ch = 0;
    wpp->lun = 0;
    wpp->pl = 0;
    wpp->last_written_blk = NULL;
}

static int blk_cmp_pri(pqueue_pri_t next, pqueue_pri_t curr)
{
    return next > curr;
}

static pqueue_pri_t blk_get_pri(void *p)
{
    return ((struct nand_block *)p)->pri;
}

static void blk_set_pri(void* p, pqueue_pri_t pri)
{
    ((struct nand_block *)p)->pri = pri;
}

static size_t blk_get_pos(void *p)
{
    return ((struct nand_block *)p)->pos;
}

static void blk_set_pos(void *p, size_t pos)
{
    ((struct nand_block *)p)->pos = pos;
}

static inline void check_addr(int a, int max)
{
    ftl_assert(a >= 0 && a < max);
}

static inline void iterate_global_write_pointer(struct ssd *ssd, struct write_pointer *wpp)
{
    struct ssdparams *spp = &ssd->sp;

    check_addr(wpp->ch, spp->nchs);
    wpp->ch++;
    if (wpp->ch == spp->nchs) {
        wpp->ch = 0;
        check_addr(wpp->lun, spp->luns_per_ch);
        wpp->lun++;
        if (wpp->lun == spp->luns_per_ch) {
            wpp->lun = 0;
            check_addr(wpp->pl, spp->pls_per_lun);
            wpp->pl++;
            if (wpp->pl == spp->pls_per_lun)
                wpp->pl = 0;
        }
    }
}

static int get_page_lifetime_class(struct ssd *ssd, uint64_t lpn)
{
    return ssd->lifetime_mgmt.pgs[lpn].lc;
}

static void update_page_lifetime_class(struct ssd *ssd, uint64_t lpn)
{
    struct lifetime_mgmt *lm = &ssd->lifetime_mgmt;
    struct logical_page *pg = &lm->pgs[lpn];
    int old_lc = pg->lc;

    if (old_lc != LIFETIME_CLASS_UNSEEN) {
        QTAILQ_REMOVE(&lm->classes[old_lc], pg, lru);
        lm->pg_cnts[old_lc]--;
    }

    for (int lc = 0; lc < NR_LIFETIME_CLASS; lc++) {
        QTAILQ_INSERT_HEAD(&lm->classes[lc], pg, lru);
        old_lc = pg->lc;
        pg->lc = lc;
        ftl_debug("Changing page lifetime class: LPN %lu, old class %d, new class %d\n",
                pg->lpn, old_lc, lc);
        if (lm->pg_cnts[lc] < lm->max_pgs[lc]) {
            lm->pg_cnts[lc]++;
            break;
        } else {
            pg = QTAILQ_LAST(&lm->classes[lc]);
            QTAILQ_REMOVE(&lm->classes[lc], pg, lru);
        }
    }
}

/**
 * May also advance the write pointer if there is no
 * free page at the beginning.
 */
static struct ppa get_new_page(struct ssd *ssd, uint64_t lpn)
{
    struct write_pointer *wpp = &ssd->wp;
    struct nand_plane *pl;
    struct nand_block *blk = NULL;
    struct nand_block **slot;
    struct ppa ppa;
    int lc = get_page_lifetime_class(ssd, lpn);

    ppa.ppa = 0;

    for (int retry = 0; retry < ssd->sp.tt_pls; retry++) {
        ppa.g.ch = wpp->ch;
        ppa.g.lun = wpp->lun;
        ppa.g.pl = wpp->pl;
        pl = get_pl(ssd, &ppa);

        if (lc == LIFETIME_CLASS_UNSEEN)
            slot = &pl->unseen;
        else
            slot = &pl->active_blks[lc];

        if (*slot != NULL) {
            blk = *slot;
            break;
        }

        if (!QTAILQ_EMPTY(&pl->free_blks)) {
            blk = QTAILQ_FIRST(&pl->free_blks);
            QTAILQ_REMOVE(&pl->free_blks, blk, tq);
            *slot = blk;
            break;
        }

        iterate_global_write_pointer(ssd, wpp);
    }

    ftl_assert(blk != NULL);

    ppa.g.blk = ARRAY_INDEX(pl->blk, blk);
    ppa.g.pg = blk->wp;

    ftl_assert(ppa.g.pl == 0);

    ftl_assert(wpp->last_written_blk == NULL);
    wpp->last_written_blk = blk;

    ftl_debug("Picking free page: ch %d, lun %d, pl %d, blk %d, pg %d\n",
            ppa.g.ch, ppa.g.lun, ppa.g.pl, ppa.g.blk, ppa.g.pg);

    return ppa;
}

static void advance_write_pointer(struct ssd *ssd)
{
    struct write_pointer *wpp = &ssd->wp;
    struct ssdparams *spp = &ssd->sp;
    struct nand_block *blk = wpp->last_written_blk;

    ftl_assert(blk != NULL);

    /* advance per-block write pointer */
    blk->wp++;
    if (blk->wp == spp->pgs_per_blk) {
        /* assume page validity has been updated */
        ftl_assert(blk->vpc >= 0 && blk->vpc <= spp->pgs_per_blk);
        ftl_assert(blk->ipc >= 0 && blk->ipc <= spp->pgs_per_blk);
        ftl_assert(blk->vpc + blk->ipc == spp->pgs_per_blk);
        blk->wp = 0;
    }
    wpp->last_written_blk = NULL;

    /* advance global write pointer */
    iterate_global_write_pointer(ssd, wpp);
}

static void check_params(struct ssdparams *spp)
{
    /*
     * we are using a general write pointer increment method now, no need to
     * force luns_per_ch and nchs to be power of 2
     */

    //ftl_assert(is_power_of_2(spp->luns_per_ch));
    //ftl_assert(is_power_of_2(spp->nchs));
    ftl_assert(spp->device_size <= spp->flash_size);
}

static void ssd_init_params(struct ssdparams *spp, uint64_t devsz)
{
    spp->secsz = SSD_SECSZ;
    spp->secs_per_pg = SSD_SECS_PER_PG;
    spp->pgs_per_blk = SSD_PGS_PER_BLK;
    spp->blks_per_pl = SSD_BLKS_PER_PL;
    spp->pls_per_lun = SSD_PLS_PER_LUN;
    spp->luns_per_ch = SSD_LUNS_PER_CH;
    spp->nchs = SSD_NCHS;

    spp->pg_rd_lat = NAND_READ_LATENCY;
    spp->pg_wr_lat = NAND_PROG_LATENCY;
    spp->blk_er_lat = NAND_ERASE_LATENCY;
    spp->ch_xfer_lat = 0;

    spp->write_buffer_size = SSD_WRITE_BUFFER_SIZE;

    /* calculated values */
    spp->secs_per_blk = spp->secs_per_pg * spp->pgs_per_blk;
    spp->secs_per_pl = spp->secs_per_blk * spp->blks_per_pl;
    spp->secs_per_lun = spp->secs_per_pl * spp->pls_per_lun;
    spp->secs_per_ch = spp->secs_per_lun * spp->luns_per_ch;
    spp->tt_secs = spp->secs_per_ch * spp->nchs;

    spp->pgs_per_pl = spp->pgs_per_blk * spp->blks_per_pl;
    spp->pgs_per_lun = spp->pgs_per_pl * spp->pls_per_lun;
    spp->pgs_per_ch = spp->pgs_per_lun * spp->luns_per_ch;
    spp->tt_pgs = spp->pgs_per_ch * spp->nchs;

    spp->blks_per_lun = spp->blks_per_pl * spp->pls_per_lun;
    spp->blks_per_ch = spp->blks_per_lun * spp->luns_per_ch;
    spp->tt_blks = spp->blks_per_ch * spp->nchs;

    spp->pls_per_ch =  spp->pls_per_lun * spp->luns_per_ch;
    spp->tt_pls = spp->pls_per_ch * spp->nchs;

    spp->tt_luns = spp->luns_per_ch * spp->nchs;

    /* line is special, put it at the end */
    spp->blks_per_line = spp->tt_luns; /* TODO: to fix under multiplanes */
    spp->pgs_per_line = spp->blks_per_line * spp->pgs_per_blk;
    spp->secs_per_line = spp->pgs_per_line * spp->secs_per_pg;
    spp->tt_lines = spp->blks_per_lun; /* TODO: to fix under multiplanes */

    spp->gc_thres_pcent = 0.75;
    spp->gc_thres_lines = (int)((1 - spp->gc_thres_pcent) * spp->tt_lines);
    spp->gc_thres_pcent_high = 0.95;
    spp->gc_thres_lines_high = (int)((1 - spp->gc_thres_pcent_high) * spp->tt_lines);
    spp->enable_gc_delay = true;

    spp->device_size = devsz;
    spp->flash_size = (uint64_t)spp->secsz *
                                spp->secs_per_pg *
                                spp->pgs_per_blk *
                                spp->blks_per_pl *
                                spp->pls_per_lun *
                                spp->luns_per_ch *
                                spp->nchs;

    check_params(spp);

    ftl_log("Device size %lu, flash size %lu, over-provision rate %.2lf%%\n",
            spp->device_size, spp->flash_size,
            ((double)spp->flash_size - spp->device_size) / spp->device_size * 100.0);
}

static void ssd_init_nand_page(struct nand_page *pg, struct ssdparams *spp)
{
    pg->nsecs = spp->secs_per_pg;
    pg->sec = g_malloc0(sizeof(nand_sec_status_t) * pg->nsecs);
    for (int i = 0; i < pg->nsecs; i++) {
        pg->sec[i] = SEC_FREE;
    }
    pg->status = PG_FREE;
}

static void ssd_init_nand_blk(struct nand_block *blk, struct ssdparams *spp)
{
    blk->npgs = spp->pgs_per_blk;
    blk->pg = g_malloc0(sizeof(struct nand_page) * blk->npgs);
    for (int i = 0; i < blk->npgs; i++) {
        ssd_init_nand_page(&blk->pg[i], spp);
    }
    blk->ipc = 0;
    blk->vpc = 0;
    blk->erase_cnt = 0;
    blk->wp = 0;
}

static void ssd_init_nand_plane(struct nand_plane *pl, struct ssdparams *spp)
{
    struct nand_block *blk;

    pl->nblks = spp->blks_per_pl;
    pl->blk = g_malloc0(sizeof(struct nand_block) * pl->nblks);
    for (int i = 0; i < pl->nblks; i++) {
        ssd_init_nand_blk(&pl->blk[i], spp);
    }
    pl->writable_blk_cnt = 0;

    QTAILQ_INIT(&pl->free_blks);
    QTAILQ_INIT(&pl->full_blks);
    pl->victim_blks = pqueue_init(spp->blks_per_pl, blk_cmp_pri, blk_get_pri,
            blk_set_pri, blk_get_pos, blk_set_pos);
    ftl_assert(pl->victim_blks != NULL);

    for (int i = 0; i < pl->nblks; i++) {
        QTAILQ_INSERT_TAIL(&pl->free_blks, &pl->blk[i], tq);
        pl->writable_blk_cnt++;
    }

    for (int i = 0; i < NR_LIFETIME_CLASS; i++) {
        blk = QTAILQ_FIRST(&pl->free_blks);
        QTAILQ_REMOVE(&pl->free_blks, blk, tq);
        pl->active_blks[i] = blk;
    }

    blk = QTAILQ_FIRST(&pl->free_blks);
    QTAILQ_REMOVE(&pl->free_blks, blk, tq);
    pl->unseen = blk;
}

static void ssd_init_nand_lun(struct nand_lun *lun, struct ssdparams *spp)
{
    lun->npls = spp->pls_per_lun;
    lun->pl = g_malloc0(sizeof(struct nand_plane) * lun->npls);
    for (int i = 0; i < lun->npls; i++) {
        ssd_init_nand_plane(&lun->pl[i], spp);
    }
    lun->next_lun_avail_time = 0;
    lun->busy = false;
}

static void ssd_init_ch(struct ssd_channel *ch, struct ssdparams *spp)
{
    ch->nluns = spp->luns_per_ch;
    ch->lun = g_malloc0(sizeof(struct nand_lun) * ch->nluns);
    for (int i = 0; i < ch->nluns; i++) {
        ssd_init_nand_lun(&ch->lun[i], spp);
    }
    ch->next_ch_avail_time = 0;
    ch->busy = 0;
}

static void ssd_init_maptbl(struct ssd *ssd)
{
    struct ssdparams *spp = &ssd->sp;

    ssd->maptbl = g_malloc0(sizeof(struct ppa) * spp->tt_pgs);
    for (int i = 0; i < spp->tt_pgs; i++) {
        ssd->maptbl[i].ppa = UNMAPPED_PPA;
    }
}

static void ssd_init_rmap(struct ssd *ssd)
{
    struct ssdparams *spp = &ssd->sp;

    ssd->rmap = g_malloc0(sizeof(uint64_t) * spp->tt_pgs);
    for (int i = 0; i < spp->tt_pgs; i++) {
        ssd->rmap[i] = INVALID_LPN;
    }
}

static void ssd_init_lifetime_mgmt(struct ssd *ssd)
{
    struct lifetime_mgmt *lm = &ssd->lifetime_mgmt;
    uint64_t tt_pgs = ssd->sp.tt_pgs;

    lm->pgs = g_malloc0(sizeof(struct logical_page) * tt_pgs);
    for (uint64_t i = 0; i < tt_pgs; i++) {
        lm->pgs[i].lpn = i;
        lm->pgs[i].lc = LIFETIME_CLASS_UNSEEN;
    }

    for (int i = 0; i < NR_LIFETIME_CLASS; i++) {
        QTAILQ_INIT(&lm->classes[i]);
        lm->pg_cnts[i] = 0;
        if (i < NR_LIFETIME_CLASS - 1) {
            lm->max_pgs[i] = ssd->sp.tt_pgs * lifetime_class_proportions[i];
            ftl_assert(lm->max_pgs[i] < tt_pgs);
            tt_pgs -= lm->max_pgs[i];
        } else {
            lm->max_pgs[NR_LIFETIME_CLASS - 1] = tt_pgs;
        }
        ftl_debug("%lu pages in lifetime class %d\n", lm->max_pgs[i], i);
    }
}

static void ssd_init_gc_info(struct ssd *ssd)
{
    struct gc_info *gc_info = &ssd->gc_info;
    struct ssdparams *spp = &ssd->sp;

    gc_info->writable_blk_cnt = spp->tt_blks;

    /* tunable parameters here */
    gc_info->global_gc_thres = spp->tt_blks * 0.25;
    gc_info->per_pl_gc_thres = spp->blks_per_pl * 0.1;

    gc_info->gc_requested = false;
}

static void ssd_init_write_buffer(struct ssd *ssd)
{
    struct ssdparams *spp = &ssd->sp;
    struct write_buffer *wbp = &ssd->wb;
    uint64_t nslots = spp->write_buffer_size / (spp->secsz * spp->secs_per_pg);

    wbp->slots = g_malloc0(nslots * sizeof(struct write_buffer_slot));
    ftl_assert(wbp->slots != NULL);

    wbp->lpn2index = g_malloc0(spp->tt_pgs * sizeof(int));
    ftl_assert(wbp->lpn2index != NULL);

    QTAILQ_INIT(&wbp->free_slots);
    QTAILQ_INIT(&wbp->used_slots);

    wbp->nslots = nslots;
    wbp->used_slot_cnt = 0;
    wbp->dirty_slot_cnt = 0;

    for (int i = 0; i < nslots; i++) {
        wbp->slots[i].lpn = INVALID_LPN;
        wbp->slots[i].dirty = false;
        QTAILQ_INSERT_HEAD(&wbp->free_slots, &wbp->slots[i], lru);
    }

    for (int i = 0; i < spp->tt_pgs; i++)
    wbp->lpn2index[i] = WB_INDEX_NULL;

    ftl_log("DRAM write buffer can hold %lu pages\n", nslots);
}

void ssd_init(FemuCtrl *n)
{
    struct ssd *ssd = n->ssd;
    struct ssdparams *spp = &ssd->sp;

    ftl_assert(ssd);

    ssd_init_params(spp, (uint64_t)n->devsz);

    /* initialize ssd internal layout architecture */
    ssd->ch = g_malloc0(sizeof(struct ssd_channel) * spp->nchs);
    for (int i = 0; i < spp->nchs; i++) {
        ssd_init_ch(&ssd->ch[i], spp);
    }

    /* initialize maptbl */
    ssd_init_maptbl(ssd);

    /* initialize rmap */
    ssd_init_rmap(ssd);

    ssd_init_lifetime_mgmt(ssd);

    ssd_init_gc_info(ssd);

    /* initialize write pointer, this is how we allocate new pages for writes */
    ssd_init_write_pointer(ssd);

    ssd_init_write_buffer(ssd);

    assert(pthread_create(&ssd->ftl_thread, NULL, ftl_thread, n) == 0);
}

static inline bool valid_ppa(struct ssd *ssd, struct ppa *ppa)
{
    struct ssdparams *spp = &ssd->sp;
    int ch = ppa->g.ch;
    int lun = ppa->g.lun;
    int pl = ppa->g.pl;
    int blk = ppa->g.blk;
    int pg = ppa->g.pg;
    int sec = ppa->g.sec;

    if (ch >= 0 && ch < spp->nchs && lun >= 0 && lun < spp->luns_per_ch && pl >=
        0 && pl < spp->pls_per_lun && blk >= 0 && blk < spp->blks_per_pl && pg
        >= 0 && pg < spp->pgs_per_blk && sec >= 0 && sec < spp->secs_per_pg)
        return true;

    return false;
}

static inline bool valid_lpn(struct ssd *ssd, uint64_t lpn)
{
    return (lpn < ssd->sp.tt_pgs);
}

static inline bool mapped_ppa(struct ppa *ppa)
{
    return !(ppa->ppa == UNMAPPED_PPA);
}

static void request_gc_if_needed(struct ssd *ssd, struct nand_plane *pl)
{
    if (pl->writable_blk_cnt < ssd->gc_info.per_pl_gc_thres)
        ssd->gc_info.gc_requested = true;
}

static uint64_t ssd_advance_status(struct ssd *ssd, struct ppa *ppa, struct
        nand_cmd *ncmd)
{
    int c = ncmd->cmd;
    uint64_t cmd_stime = (ncmd->stime == 0) ? \
        qemu_clock_get_ns(QEMU_CLOCK_REALTIME) : ncmd->stime;
    uint64_t nand_stime;
    struct ssdparams *spp = &ssd->sp;
    struct nand_lun *lun = get_lun(ssd, ppa);
    uint64_t lat = 0;

    switch (c) {
    case NAND_READ:
        /* read: perform NAND cmd first */
        nand_stime = (lun->next_lun_avail_time < cmd_stime) ? cmd_stime : \
                     lun->next_lun_avail_time;
        lun->next_lun_avail_time = nand_stime + spp->pg_rd_lat;
        lat = lun->next_lun_avail_time - cmd_stime;
#if 0
        lun->next_lun_avail_time = nand_stime + spp->pg_rd_lat;

        /* read: then data transfer through channel */
        chnl_stime = (ch->next_ch_avail_time < lun->next_lun_avail_time) ? \
            lun->next_lun_avail_time : ch->next_ch_avail_time;
        ch->next_ch_avail_time = chnl_stime + spp->ch_xfer_lat;

        lat = ch->next_ch_avail_time - cmd_stime;
#endif
        break;

    case NAND_WRITE:
        /* write: transfer data through channel first */
        nand_stime = (lun->next_lun_avail_time < cmd_stime) ? cmd_stime : \
                     lun->next_lun_avail_time;
        if (ncmd->type == USER_IO) {
            lun->next_lun_avail_time = nand_stime + spp->pg_wr_lat;
        } else {
            lun->next_lun_avail_time = nand_stime + spp->pg_wr_lat;
        }
        lat = lun->next_lun_avail_time - cmd_stime;

#if 0
        chnl_stime = (ch->next_ch_avail_time < cmd_stime) ? cmd_stime : \
                     ch->next_ch_avail_time;
        ch->next_ch_avail_time = chnl_stime + spp->ch_xfer_lat;

        /* write: then do NAND program */
        nand_stime = (lun->next_lun_avail_time < ch->next_ch_avail_time) ? \
            ch->next_ch_avail_time : lun->next_lun_avail_time;
        lun->next_lun_avail_time = nand_stime + spp->pg_wr_lat;

        lat = lun->next_lun_avail_time - cmd_stime;
#endif
        break;

    case NAND_ERASE:
        /* erase: only need to advance NAND status */
        nand_stime = (lun->next_lun_avail_time < cmd_stime) ? cmd_stime : \
                     lun->next_lun_avail_time;
        lun->next_lun_avail_time = nand_stime + spp->blk_er_lat;

        lat = lun->next_lun_avail_time - cmd_stime;
        break;

    default:
        ftl_err("Unsupported NAND command: 0x%x\n", c);
    }

    return lat;
}

/* update SSD status about one page from PG_VALID -> PG_VALID */
static void mark_page_invalid(struct ssd *ssd, struct ppa *ppa)
{
    struct ssdparams *spp = &ssd->sp;
    struct nand_plane *pl = NULL;
    struct nand_block *blk = NULL;
    struct nand_page *pg = NULL;
    bool was_full_block = false;

    /* update corresponding page status */
    pg = get_pg(ssd, ppa);
    ftl_assert(pg->status == PG_VALID);
    pg->status = PG_INVALID;

    /* update corresponding block status */
    blk = get_blk(ssd, ppa);

    if (blk->vpc == spp->pgs_per_blk)
        was_full_block = true;

    ftl_assert(blk->ipc >= 0 && blk->ipc < spp->pgs_per_blk);
    blk->ipc++;
    ftl_assert(blk->vpc > 0 && blk->vpc <= spp->pgs_per_blk);
    blk->vpc--;

    if (blk->vpc + blk->ipc == spp->pgs_per_blk) {
        pl = get_pl(ssd, ppa);
        if (was_full_block) {
            /* move from full block list to victim queue */
            QTAILQ_REMOVE(&pl->full_blks, blk, tq);
            blk_set_pri(blk, blk->vpc);
            pqueue_insert(pl->victim_blks, blk);
        } else {
            pqueue_change_priority(pl->victim_blks, blk->vpc, blk);
        }
    }
}

static void mark_page_valid(struct ssd *ssd, struct ppa *ppa, uint64_t lpn)
{
    struct nand_plane *pl = NULL;
    struct nand_block *blk = NULL;
    struct nand_page *pg = NULL;
    int lc = get_page_lifetime_class(ssd, lpn);

    /* update page status */
    pg = get_pg(ssd, ppa);
    ftl_assert(pg->status == PG_FREE);
    pg->status = PG_VALID;

    /* update corresponding block status */
    blk = get_blk(ssd, ppa);
    ftl_assert(blk->vpc >= 0 && blk->vpc < ssd->sp.pgs_per_blk);
    blk->vpc++;

    pl = get_pl(ssd, ppa);
    if (blk->vpc + blk->ipc == ssd->sp.pgs_per_blk) {
        /* no more free pages in the block */
        ftl_debug("Block full: ch %d, lun %d, pl %d, blk %d\n",
                ppa->g.ch, ppa->g.lun, ppa->g.pl, ppa->g.blk);

        pl->writable_blk_cnt--;
        ssd->gc_info.writable_blk_cnt--;

        if (blk->ipc == 0) {
            /* all pages valid */
            QTAILQ_INSERT_TAIL(&pl->full_blks, blk, tq);
        } else {
            /* has invalid pages; throw to victim queue */
            blk_set_pri(blk, blk->vpc);
            pqueue_insert(pl->victim_blks, blk);
        }

        if (lc == LIFETIME_CLASS_UNSEEN)
            pl->unseen = NULL;
        else
            pl->active_blks[lc] = NULL;
    }

    request_gc_if_needed(ssd, pl);
}

static uint64_t write_buffer_evict_page(struct ssd *ssd, uint64_t stime)
{
    struct write_buffer *wbp = &ssd->wb;
    struct write_buffer_slot *slot = QTAILQ_LAST(&wbp->used_slots);
    uint64_t lpn = slot->lpn;
    uint64_t lat = 0;

    if (slot->dirty) {
        struct ppa ppa = get_new_page(ssd, lpn);
        struct nand_cmd cmd;

        set_maptbl_ent(ssd, lpn, &ppa);
        set_rmap_ent(ssd, lpn, &ppa);

        mark_page_valid(ssd, &ppa, lpn);

        advance_write_pointer(ssd);

        cmd.type = USER_IO;
        cmd.cmd = NAND_WRITE;
        cmd.stime = stime;
        lat = ssd_advance_status(ssd, &ppa, &cmd);

        wbp->dirty_slot_cnt--;
    }

    QTAILQ_REMOVE(&wbp->used_slots, slot, lru);
    QTAILQ_INSERT_HEAD(&wbp->free_slots, slot, lru);

    wbp->lpn2index[lpn] = WB_INDEX_NULL;
    wbp->used_slot_cnt--;

    return lat;
}

static uint64_t write_buffer_insert_page(struct ssd *ssd, uint64_t lpn, uint64_t stime, bool from_nand)
{
    struct write_buffer *wbp = &ssd->wb;
    struct write_buffer_slot *slot = NULL;
    struct ppa ppa;
    uint64_t lat = 0;

    ftl_assert(wbp->used_slot_cnt <= wbp->nslots);
    ftl_assert(wbp->lpn2index[lpn] == WB_INDEX_NULL);

    if (from_nand) {
        ppa = get_maptbl_ent(ssd, lpn);
        /* this may happen during system startup when the kernel reads the SSD's superblock */
        if (!mapped_ppa(&ppa) || !valid_ppa(ssd, &ppa))
            return 0;
    }

    if (wbp->used_slot_cnt == wbp->nslots)
        lat = write_buffer_evict_page(ssd, stime);

    ftl_assert(wbp->used_slot_cnt < wbp->nslots);
    ftl_assert(!QTAILQ_EMPTY(&wbp->free_slots));

    slot = QTAILQ_FIRST(&wbp->free_slots);
    QTAILQ_REMOVE(&wbp->free_slots, slot, lru);
    QTAILQ_INSERT_HEAD(&wbp->used_slots, slot, lru);
    wbp->used_slot_cnt++;

    slot->lpn = lpn;
    wbp->lpn2index[lpn] = ARRAY_INDEX(wbp->slots, slot);

    if (from_nand) {
        struct nand_cmd cmd;

        cmd.type = USER_IO;
        cmd.cmd = NAND_READ;
        cmd.stime = stime;
        lat += ssd_advance_status(ssd, &ppa, &cmd);

        slot->dirty = false;
    } else {
        slot->dirty = true;
        wbp->dirty_slot_cnt++;
    }

    return lat;
}

static uint64_t read_page_from_write_buffer(struct ssd *ssd, uint64_t lpn, uint64_t stime)
{
    struct write_buffer *wbp = &ssd->wb;
    int index = wbp->lpn2index[lpn];

    if (index != WB_INDEX_NULL) {
        /* page already in write buffer */
        struct write_buffer_slot *slot = &wbp->slots[index];

        QTAILQ_REMOVE(&wbp->used_slots, slot, lru);
        QTAILQ_INSERT_HEAD(&wbp->used_slots, slot, lru);

        return 0;
    }

    return write_buffer_insert_page(ssd, lpn, stime, true);
}

static uint64_t write_page_to_write_buffer(struct ssd *ssd, uint64_t lpn, uint64_t stime)
{
    struct write_buffer *wbp = &ssd->wb;
    int index = wbp->lpn2index[lpn];
    struct ppa ppa = get_maptbl_ent(ssd, lpn);

    if (index != WB_INDEX_NULL) {
        /* page already in write buffer */
        struct write_buffer_slot *slot = &wbp->slots[index];

        QTAILQ_REMOVE(&wbp->used_slots, slot, lru);
        QTAILQ_INSERT_HEAD(&wbp->used_slots, slot, lru);

        if (!slot->dirty) {
            ftl_assert(mapped_ppa(&ppa) && valid_ppa(ssd, &ppa));
            ftl_assert(get_pg(ssd, &ppa)->status == PG_VALID);

            mark_page_invalid(ssd, &ppa);
            set_rmap_ent(ssd, INVALID_LPN, &ppa);
            slot->dirty = true;
            wbp->dirty_slot_cnt++;
        }

        return 0;
    }

    /* page not in write buffer */
    if (mapped_ppa(&ppa) && valid_ppa(ssd, &ppa)) {
        mark_page_invalid(ssd, &ppa);
        set_rmap_ent(ssd, INVALID_LPN, &ppa);
    }

    return write_buffer_insert_page(ssd, lpn, stime, false);
}

static void mark_block_free(struct ssd *ssd, struct ppa *ppa)
{
    struct ssdparams *spp = &ssd->sp;
    struct nand_plane *pl = get_pl(ssd, ppa);
    struct nand_block *blk = get_blk(ssd, ppa);
    struct nand_page *pg = NULL;

    for (int i = 0; i < spp->pgs_per_blk; i++) {
        /* reset page status */
        pg = &blk->pg[i];
        ftl_assert(pg->nsecs == spp->secs_per_pg);
        pg->status = PG_FREE;
    }

    /* reset block status */
    ftl_assert(blk->npgs == spp->pgs_per_blk);
    blk->ipc = 0;
    blk->vpc = 0;
    blk->erase_cnt++;
    ftl_assert(blk->wp == 0);

    QTAILQ_INSERT_TAIL(&pl->free_blks, blk, tq);
    pl->writable_blk_cnt++;
    ssd->gc_info.writable_blk_cnt++;
}

static void gc_copy_page(struct ssd *ssd, struct ppa *ppa)
{
    uint64_t lpn = get_rmap_ent(ssd, ppa);
    struct write_buffer *wbp = &ssd->wb;
    int index;

    ftl_assert(valid_lpn(ssd, lpn));

    index = wbp->lpn2index[lpn];
    if (index != WB_INDEX_NULL) {
        /* page in write buffer; simply mark it as dirty */
        struct write_buffer_slot *slot = &wbp->slots[index];

        ftl_assert(!slot->dirty);
        slot->dirty = true;
        wbp->dirty_slot_cnt++;
    } else {
        /* page not in write buffer; need to copy it */
        struct ppa new_ppa;
        struct nand_lun *new_lun;

        new_ppa = get_new_page(ssd, lpn);

        set_maptbl_ent(ssd, lpn, &new_ppa);
        set_rmap_ent(ssd, lpn, &new_ppa);
        mark_page_valid(ssd, &new_ppa, lpn);

        advance_write_pointer(ssd);

        if (ssd->sp.enable_gc_delay) {
            struct nand_cmd cmd;

            cmd.type = GC_IO;
            cmd.cmd = NAND_READ;
            cmd.stime = 0;
            ssd_advance_status(ssd, ppa, &cmd);

            cmd.type = GC_IO;
            cmd.cmd = NAND_WRITE;
            cmd.stime = 0;
            ssd_advance_status(ssd, &new_ppa, &cmd);
        }

        new_lun = get_lun(ssd, &new_ppa);
        new_lun->gc_endtime = new_lun->next_lun_avail_time;
    }
}

/* here ppa identifies the block we want to clean */
static void clean_one_block(struct ssd *ssd, struct ppa *ppa)
{
    struct ssdparams *spp = &ssd->sp;
    struct nand_page *pg_iter = NULL;
    int cnt = 0;

    for (int pg = 0; pg < spp->pgs_per_blk; pg++) {
        ppa->g.pg = pg;
        pg_iter = get_pg(ssd, ppa);
        /* there shouldn't be any free page in victim blocks */
        ftl_assert(pg_iter->status != PG_FREE);
        if (pg_iter->status == PG_VALID) {
            gc_copy_page(ssd, ppa);
            cnt++;
        }
    }

    ftl_assert(get_blk(ssd, ppa)->vpc == cnt);
}

static void gc_one_block(struct ssd *ssd, struct ppa *ppa)
{
    struct nand_lun *lun = get_lun(ssd, ppa);
    struct nand_block *blk = get_blk(ssd, ppa);

    ftl_debug("GC-ing block: ch %d, lun %d, pl %d, blk %d, vpc %d, ipc %d\n",
            ppa->g.ch, ppa->g.lun, ppa->g.pl, ppa->g.blk, blk->vpc, blk->ipc);

    clean_one_block(ssd, ppa);
    mark_block_free(ssd, ppa);

    if (ssd->sp.enable_gc_delay) {
        struct nand_cmd gce;

        gce.type = GC_IO;
        gce.cmd = NAND_ERASE;
        gce.stime = 0;

        ssd_advance_status(ssd, ppa, &gce);
    }

    lun->gc_endtime = lun->next_lun_avail_time;
}

// static int do_gc(struct ssd *ssd, bool force)
// {
//     struct line *victim_line = NULL;
//     struct ssdparams *spp = &ssd->sp;
//     struct nand_lun *lunp;
//     struct ppa ppa;
//     int ch, lun;

//     victim_line = select_victim_line(ssd, force);
//     if (!victim_line) {
//         return -1;
//     }

//     ppa.g.blk = victim_line->id;
//     ftl_debug("GC-ing line:%d,ipc=%d,victim=%d,full=%d,free=%d\n", ppa.g.blk,
//               victim_line->ipc, ssd->lm.victim_line_cnt, ssd->lm.full_line_cnt,
//               ssd->lm.free_line_cnt);

//     /* copy back valid data */
//     for (ch = 0; ch < spp->nchs; ch++) {
//         for (lun = 0; lun < spp->luns_per_ch; lun++) {
//             ppa.g.ch = ch;
//             ppa.g.lun = lun;
//             ppa.g.pl = 0;
//             lunp = get_lun(ssd, &ppa);
//             clean_one_block(ssd, &ppa);
//             mark_block_free(ssd, &ppa);

//             if (spp->enable_gc_delay) {
//                 struct nand_cmd gce;
//                 gce.type = GC_IO;
//                 gce.cmd = NAND_ERASE;
//                 gce.stime = 0;
//                 ssd_advance_status(ssd, &ppa, &gce);
//             }

//             lunp->gc_endtime = lunp->next_lun_avail_time;
//         }
//     }

//     /* update line status */
//     mark_line_free(ssd, &ppa);

//     return 0;
// }

static struct nand_block *get_victim_block(struct ssd *ssd, struct nand_plane *pl, bool foreground)
{
    struct nand_block *blk = pqueue_peek(pl->victim_blks);

    if (blk == NULL)
        return NULL;

    if (pl->writable_blk_cnt < ssd->gc_info.per_pl_gc_thres ||
            (!foreground && blk->vpc < blk->npgs / 4)) {
        pqueue_pop(pl->victim_blks);
        return blk;
    }

    return NULL;
}

/* attempt to clean one block in each plane */
static int do_gc(struct ssd *ssd, bool foreground)
{
    struct ssd_channel *ch;
    struct nand_lun *lun;
    struct nand_plane *pl;
    struct nand_block *blk;
    struct ppa ppa;
    int ret = -1;

    ssd->gc_info.gc_requested = false;

    ftl_debug("GC triggered: foreground %d, writable block count: %lu\n",
            (int)foreground, ssd->gc_info.writable_blk_cnt);

    FOR_EACH_PLANE(ssd, ch, lun, pl) {
        blk = get_victim_block(ssd, pl, foreground);
        if (blk == NULL)
            continue;
        ret = 0;

        set_ppa_from_pointers(&ppa, ssd, ch, lun, pl, blk, NULL);
        gc_one_block(ssd, &ppa);
    }

    return ret;
}

static uint64_t ssd_read(struct ssd *ssd, NvmeRequest *req)
{
    struct ssdparams *spp = &ssd->sp;
    uint64_t lba = req->slba;
    int nsecs = req->nlb;
    uint64_t start_lpn = lba / spp->secs_per_pg;
    uint64_t end_lpn = (lba + nsecs - 1) / spp->secs_per_pg;
    uint64_t lpn;
    uint64_t sublat, maxlat = 0;

    if (end_lpn >= spp->tt_pgs) {
        ftl_err("start_lpn=%"PRIu64",tt_pgs=%d\n", start_lpn, ssd->sp.tt_pgs);
    }

    /* normal IO read path */
    for (lpn = start_lpn; lpn <= end_lpn; lpn++) {
        sublat = read_page_from_write_buffer(ssd, lpn, req->stime);
        if (sublat > maxlat)
            maxlat = sublat;
    }

    return maxlat;
}

static uint64_t ssd_write(struct ssd *ssd, NvmeRequest *req)
{
    uint64_t lba = req->slba;
    struct ssdparams *spp = &ssd->sp;
    int len = req->nlb;
    uint64_t start_lpn = lba / spp->secs_per_pg;
    uint64_t end_lpn = (lba + len - 1) / spp->secs_per_pg;
    uint64_t lpn;
    uint64_t curlat = 0, maxlat = 0;
    int r;

    if (end_lpn >= spp->tt_pgs) {
        ftl_err("start_lpn=%"PRIu64",tt_pgs=%d\n", start_lpn, ssd->sp.tt_pgs);
    }

    while (should_gc_foreground(ssd)) {
        /* foreground GC with stricter victim block selection */
        r = do_gc(ssd, true);
        if (r < 0)
            break;
    }

    for (lpn = start_lpn; lpn <= end_lpn; lpn++) {
        update_page_lifetime_class(ssd, lpn);
        curlat = write_page_to_write_buffer(ssd, lpn, req->stime);
        if (curlat > maxlat)
            maxlat = curlat;
    }

    return maxlat;
}

static void *ftl_thread(void *arg)
{
    FemuCtrl *n = (FemuCtrl *)arg;
    struct ssd *ssd = n->ssd;
    NvmeRequest *req = NULL;
    uint64_t lat = 0;
    int rc;
    int i;

    while (!*(ssd->dataplane_started_ptr)) {
        usleep(100000);
    }

    /* FIXME: not safe, to handle ->to_ftl and ->to_poller gracefully */
    ssd->to_ftl = n->to_ftl;
    ssd->to_poller = n->to_poller;

    while (1) {
        for (i = 0; i < n->num_poller; i++) {
            if (!ssd->to_ftl[i] || !femu_ring_count(ssd->to_ftl[i]))
                continue;

            rc = femu_ring_dequeue(ssd->to_ftl[i], (void *)&req, 1);
            if (rc != 1) {
                printf("FEMU: FTL to_ftl dequeue failed\n");
            }

            ftl_assert(req);
            switch (req->cmd.opcode) {
            case NVME_CMD_WRITE:
                lat = ssd_write(ssd, req);
                break;
            case NVME_CMD_READ:
                lat = ssd_read(ssd, req);
                break;
            case NVME_CMD_DSM:
                lat = 0;
                break;
            default:
                //ftl_err("FTL received unkown request type, ERROR\n");
                ;
            }

            req->reqlat = lat;
            req->expire_time += lat;

            rc = femu_ring_enqueue(ssd->to_poller[i], (void *)&req, 1);
            if (rc != 1) {
                ftl_err("FTL to_poller enqueue failed\n");
            }

            /* background GC with more relaxed victim block selection */
            if (should_gc_background(ssd)) {
                do_gc(ssd, false);
            }
        }
    }

    return NULL;
}

#endif /* USE_LIFETIME_FTL */
