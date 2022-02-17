# FEMU-Sim

An SSD simulator derived from FEMU, so that we don't need to mess with VMs.

## Building

```
mkdir build
cd build
cmake ..
make
```

A simple test program can be found under `build/bin`.

## Porting an FTL from FEMU

Make sure to check over-provision rate:

```C
static void check_params(struct ssdparams *spp)
{
    ftl_assert(spp->device_size <= spp->flash_size);
}
```

Replace all `qemu_thread_create` with `pthread_create` and all `QemuThread` with `pthread_t`:

```C
void ssd_init(FemuCtrl *n)
{
    /* ...... */
    assert(pthread_create(&ssd->ftl_thread, NULL, ftl_thread, n) == 0);
    /* ...... */
}
```

Iteration boundary in `ftl_thread`:

```C
static void *ftl_thread(void *arg)
{
    /* ...... */
    while (1) {
        for (int i = 0; i < n->num_poller; i++) {
            /* ...... */
        }
    }
    /* ...... */
}
```

Disable latency emulation when needed:

```C
static void *ftl_thread(void *arg)
{
    /* ...... */
    if (n->enable_latency) {
        req->reqlat = lat;
        req->expire_time += lat;
    }
    /* ...... */
}
```
