#ifndef __FEMU_H__
#define __FEMU_H__

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"{
#endif

struct FemuCtrl;

struct FemuCtrl *femu_init(size_t ssd_size, bool enable_backend, bool enable_latency);
int femu_read(struct FemuCtrl *femu, size_t len, off_t offset, void *arg);
int femu_write(struct FemuCtrl *femu, size_t len, off_t offset, void *arg);
int femu_reset(struct FemuCtrl *femu, size_t len, off_t offset, void *arg);
void get_zns_meta(uint64_t *meta);

#ifdef __cplusplus
}
#endif

#endif
