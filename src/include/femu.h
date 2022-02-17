#ifndef __FEMU_H__
#define __FEMU_H__

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

struct FemuCtrl;

struct FemuCtrl *femu_init(size_t ssd_size, bool enable_backend, bool enable_latency);
int femu_read(struct FemuCtrl *femu, void *buf, size_t len, off_t offset);
int femu_write(struct FemuCtrl *femu, const void *buf, size_t len, off_t offset);

#endif
