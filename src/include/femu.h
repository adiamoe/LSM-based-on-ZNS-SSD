#ifndef __FEMU_H__
#define __FEMU_H__

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"{
#endif

struct FemuCtrl;

struct FemuCtrl *femu_init(uint64_t ssd_size, bool enable_backend, bool enable_latency);
int femu_read(struct FemuCtrl *femu, uint64_t len, uint64_t offset, void *arg, bool wait);
int femu_write(struct FemuCtrl *femu, uint64_t len, uint64_t offset, void *arg, bool wait);
int femu_reset(struct FemuCtrl *femu, uint64_t len, uint64_t offset, void *arg);
void get_zns_meta(uint64_t *meta);

#ifdef __cplusplus
}
#endif

#endif
