#include "ftl.h"

//#define FEMU_DEBUG_FTL

#ifndef USE_LIFETIME_FTL

static inline uint32_t zns_zone_idx(FemuCtrl *n, uint64_t slba, struct ssdparams *spp)
{
    return slba / (n->zone_size / spp->secsz);
}

static inline NvmeZone *zns_get_zone_by_lba(FemuCtrl *n, uint64_t slba, struct ssdparams *spp)
{
    uint32_t zone_idx = zns_zone_idx(n, slba, spp);

    assert(zone_idx < n->num_zones);
    return &n->zone_array[zone_idx];
}

void printppa(struct ppa *ppa) {
    printf("ppa.chip = %lu, ppa.lun = %lu, ppa.blk = %lu, ppa.page = %lu\n", ppa->ch, ppa->lun, ppa->blk, ppa->pg);
}

static void zns_advance_write_pointer(int num_fcg, NvmeZone *zone, struct ssd *ssd) {
    struct ssdparams *spp = &ssd->sp;
    struct zns_write_pointer *wpp = &zone->wpp;

    int nchs = spp->nchs / num_fcg;

    wpp->ch++;
    if(wpp->ch % nchs == 0) {
        wpp->ch = nchs * wpp->FCGid;
        wpp->blk++;
        uint64_t block_size = spp->pgs_per_blk * spp->secs_per_pg * spp->secsz;
        int end = zone->d.zone_cap / block_size / (spp->nchs / num_fcg);
        if(wpp->blk % end == 0) {
            unsigned blk = (end * (zone->ZoneID / num_fcg)) % spp->blks_per_pl;
            wpp->blk = blk;
            wpp->pg++;
            if(wpp->pg == spp->pgs_per_blk) {
                zone->d.state = NVME_ZONE_STATE_FULL;
            }
        }
    }
}

static struct ppa get_zns_page(NvmeZone *zone) {
    struct zns_write_pointer *wpp = &zone->wpp;
    struct ppa ppa;
    ppa.blk = wpp->blk;
    ppa.ch = wpp->ch;
    ppa.lun = wpp->lun;
    ppa.pg = wpp->pg;
    ppa.pl = wpp->pl;

    return ppa;
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
}


static void ssd_init_nand_blk(struct nand_block *blk, struct ssdparams *spp)
{
    blk->npgs = spp->pgs_per_blk;
    blk->erase_cnt = 0;
}

static void ssd_init_nand_plane(struct nand_plane *pl, struct ssdparams *spp)
{
    pl->nblks = spp->blks_per_pl;
    pl->blk = g_malloc0(sizeof(struct nand_block) * pl->nblks);
    for (int i = 0; i < pl->nblks; i++) {
        ssd_init_nand_blk(&pl->blk[i], spp);
    }
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

void ssd_init(FemuCtrl *n)
{
    struct ssd *ssd = n->ssd;
    struct ssdparams *spp = &ssd->sp;

    ftl_assert(ssd);

    ssd_init_params(spp, n->devsz);

    /* initialize ssd internal layout architecture */
    ssd->ch = g_malloc0(sizeof(struct ssd_channel) * spp->nchs);
    for (int i = 0; i < spp->nchs; i++) {
        ssd_init_ch(&ssd->ch[i], spp);
    }
}

void zns_init(FemuCtrl *n) {
    struct ssdparams *spp = &n->ssd->sp;
    n->zone_size = ZONE_SIZE;
    n->num_zones = n->devsz / n->zone_size;
    n->num_fcg = NUM_FCGS;
    uint64_t start = 0, zone_size = n->zone_size;
    uint64_t block_size = spp->secsz * spp->secs_per_pg * spp->pgs_per_blk;
    uint8_t num_fcg = n->num_fcg;
    NvmeZone *zone;
    int i;

    n->zone_array = g_malloc0((n->num_zones) * sizeof(NvmeZone));
    zone = n->zone_array;
    for (i = 0; i < n->num_zones; i++, zone++) {
        zone->ZoneID = i;
        zone->d.state = NVME_ZONE_STATE_EMPTY;
        zone->write_ptr = start / SSD_SECSZ;
        zone->d.start_lba = start / SSD_SECSZ;
        zone->d.zone_cap = zone_size;
        start += zone_size;

        struct zns_write_pointer *page = &zone->wpp;
        page->FCGid = i % num_fcg;
        page->ch = (spp->nchs / num_fcg) * (i % num_fcg);
        page->lun = i / (spp->blks_per_pl * block_size * spp->nchs / zone_size);
        page->blk = ((zone_size / block_size / (spp->nchs / num_fcg)) * (i / num_fcg)) % spp->blks_per_pl;
        page->pg = 0;
        page->pl = 0;
    }
}

static inline struct ssd_channel *get_ch(struct ssd *ssd, struct ppa *ppa)
{
    return &(ssd->ch[ppa->ch]);
}

static inline struct nand_lun *get_lun(struct ssd *ssd, struct ppa *ppa)
{
    struct ssd_channel *ch = get_ch(ssd, ppa);
    return &(ch->lun[ppa->lun]);
}

static inline struct nand_plane *get_pl(struct ssd *ssd, struct ppa *ppa)
{
    struct nand_lun *lun = get_lun(ssd, ppa);
    return &(lun->pl[ppa->pl]);
}

static inline struct nand_block *get_blk(struct ssd *ssd, struct ppa *ppa)
{
    struct nand_plane *pl = get_pl(ssd, ppa);
    return &(pl->blk[ppa->blk]);
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

static bool get_next_page(NvmeZone *zone, int num_fcg, struct ssd *ssd, struct ppa *page) {
    struct ssdparams *spp = &ssd->sp;

    int nchs = spp->nchs / num_fcg;

    page->ch++;
    if(page->ch % nchs == 0) {
        page->ch = nchs * zone->wpp.FCGid;
        page->blk++;
        uint64_t block_size = spp->secs_per_blk * spp->secsz;
        unsigned end = zone->d.zone_cap / block_size / (spp->nchs / num_fcg);
        if(page->blk % end == 0) {
            page->blk = (end * (zone->ZoneID / num_fcg)) % spp->blks_per_pl;
            page->pg++;
            if(page->pg == spp->pgs_per_blk) {
                return true;
            }
        }
    }
    return false;
}

//todo:test
static struct ppa locate_page(int num_fcg, struct ssdparams *spp, NvmeZone *zone, uint64_t start_lpn) {
    struct ppa ppa;
    int id = zone->ZoneID;
    int num_chip = spp->nchs / num_fcg;
    uint64_t block_size = spp->secs_per_blk * spp->secsz;
    unsigned end = zone->d.zone_cap / block_size / (spp->nchs / num_fcg);
    unsigned block_start = (end * (id / num_fcg)) % spp->blks_per_pl;
    ppa.ch = (start_lpn % num_chip) + zone->wpp.FCGid * num_chip;
    ppa.lun = zone->wpp.lun;
    ppa.blk = (start_lpn / num_chip) % end + block_start;
    ppa.pg = start_lpn / num_chip / end;
    ppa.pl = 0;
    return ppa;
}

static void zns_reset_wpp(NvmeZone *zone, struct ssdparams *spp, int num_fcg, uint64_t block_size) {
    struct zns_write_pointer *page = &zone->wpp;
    int id = zone->ZoneID;
    page->FCGid = id % num_fcg;
    page->ch = (spp->nchs / num_fcg) * (id % num_fcg);
    page->lun = id / (spp->blks_per_pl * block_size * spp->nchs / zone->d.zone_cap);
    page->blk = ((zone->d.zone_cap / block_size / (spp->nchs / num_fcg)) * (id / num_fcg)) % spp->blks_per_pl;
    page->pg = 0;
    page->pl = 0;
    zone->d.state = NVME_ZONE_STATE_EMPTY;
}

void zns_reset(FemuCtrl *n, NvmeRequest *req) {
    uint64_t slba = req->slba;
    uint8_t num_fcg = n->num_fcg;
    struct ssd *ssd = n->ssd;
    struct ssdparams *spp = &ssd->sp;
    NvmeZone *zone = zns_get_zone_by_lba(n, slba, spp);

    if(zone->d.state != NVME_ZONE_STATE_FULL)
        return;

    struct ppa ppa;
    struct nand_lun *lunp;

    uint64_t block_size = spp->pgs_per_blk * spp->secs_per_pg * spp->secsz;
    zone->write_ptr = zone->d.start_lba;
    zns_reset_wpp(zone, spp, num_fcg, block_size);

    unsigned end = zone->d.zone_cap / block_size / (spp->nchs / num_fcg);
    for (unsigned ch = zone->wpp.ch; ch < (spp->nchs / num_fcg * (zone->wpp.FCGid+1)); ch++) {
        for (unsigned blk = zone->wpp.blk; blk == zone->wpp.blk || blk % end != 0; blk++) {
            ppa.ch = ch;
            ppa.lun = zone->wpp.lun;
            ppa.pl = 0;
            ppa.blk = blk;
            //printppa(&ppa);
            lunp = get_lun(ssd, &ppa);

            if (spp->enable_gc_delay) {
                struct nand_cmd gce;
                gce.type = GC_IO;
                gce.cmd = NAND_ERASE;
                gce.stime = 0;
                ssd_advance_status(ssd, &ppa, &gce);
            }

            lunp->gc_endtime = lunp->next_lun_avail_time;
        }
    }
}

uint64_t zns_read(FemuCtrl *n, NvmeRequest *req) {
    uint64_t slba = req->slba;
    uint16_t nlb = req->nlb;

    struct ssd *ssd = n->ssd;
    struct ssdparams *spp = &ssd->sp;

    NvmeZone *zone_start = zns_get_zone_by_lba(n, slba, spp);
    NvmeZone *zone_end = zns_get_zone_by_lba(n, slba + nlb, spp);

    uint64_t start_lpn = slba / spp->secs_per_pg;
    uint64_t end_lpn = (slba + nlb - 1) / spp->secs_per_pg;
    uint64_t lpn_in_zone = start_lpn % (n->zone_size / (spp->secs_per_pg * spp->secsz));

    struct ppa ppa = locate_page(n->num_fcg, spp, zone_start, lpn_in_zone);

    uint64_t sublat, maxlat = 0;

    if (end_lpn >= spp->tt_pgs) {
        ftl_err("start_lpn=%"PRIu64",tt_pgs=%d\n", start_lpn, ssd->sp.tt_pgs);
    }

    /* normal IO read path */
    for (uint64_t lpn = start_lpn; lpn <= end_lpn; lpn++) {
        //printf("req %d:\t", req->id);
        //printppa(&ppa);
        struct nand_cmd srd;
        srd.type = USER_IO;
        srd.cmd = NAND_READ;
        srd.stime = req->stime;
        sublat = ssd_advance_status(ssd, &ppa, &srd);

        if(get_next_page(zone_start, n->num_fcg, ssd, &ppa) && zone_start != zone_end) {
            zone_start++;
            ppa = locate_page(n->num_fcg, spp, zone_start, 0);
        }

        maxlat = (sublat > maxlat) ? sublat : maxlat;
    }

    return maxlat;
}

void zns_check_zone_write(FemuCtrl *n, NvmeZone *zone, uint64_t slba, uint16_t nlb) {
    struct ssd *ssd = n->ssd;
    struct ssdparams *spp = &ssd->sp;

    if(slba + nlb > spp->tt_secs)
        ftl_err("start_lpn=%"PRIu64",tt_pgs=%d\n", slba, spp->tt_secs);

    if(slba + nlb > zone->d.start_lba+zone->d.zone_cap)
        ftl_err("NVME_ZONE_BOUNDARY_ERROR\n");

    if(zone->d.state == NVME_ZONE_STATE_FULL)
        ftl_err("NVME_ZONE_FULL\n");

    if(zone->write_ptr != slba)
        ftl_err("NVME_ZONE_INVALID_WRITE: %lu, %lu, %d\n", zone->write_ptr, slba, zone->ZoneID);
}

uint64_t zns_write(FemuCtrl *n, NvmeRequest *req) {
    uint64_t slba = req->slba;
    uint16_t nlb = req->nlb;
    uint8_t num_fcg = n->num_fcg;

    struct ssd *ssd = n->ssd;
    struct ssdparams *spp = &ssd->sp;

    NvmeZone *zone = zns_get_zone_by_lba(n, slba, spp);
    //printf("%lu, %lu, %lu, %d\n", zone->write_ptr, slba, nlb, zone->ZoneID);

    zns_check_zone_write(n, zone, slba, nlb);

    //update zns writer pointer
    zone->write_ptr += nlb;

    uint64_t start_lpn = slba / spp->secs_per_pg;
    uint64_t end_lpn = (slba + nlb - 1) / spp->secs_per_pg;
    struct ppa ppa;
    uint64_t lpn;
    uint64_t curlat = 0, maxlat = 0;

    for (lpn = start_lpn; lpn <= end_lpn; lpn++) {

        ppa = get_zns_page(zone);

        zns_advance_write_pointer(num_fcg, zone, ssd);

        struct nand_cmd swr;
        swr.type = USER_IO;
        swr.cmd = NAND_WRITE;
        swr.stime = req->stime;
        curlat = ssd_advance_status(ssd, &ppa, &swr);
        maxlat = (curlat > maxlat) ? curlat : maxlat;
    }
    return maxlat;
}

#endif /* USE_LIFETIME_FTL */
