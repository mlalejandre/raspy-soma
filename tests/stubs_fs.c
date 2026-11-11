/* Sustitutos para probar fs.c en host: RAM-disk de 16 MiB, mutex nulos, red/RTC/imagenes. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "host_stubs.h"
#include "mutex.h"
#include "rtc.h"
#include "net.h"
#include "image.h"
#include "virtio_net.h"

#define DISK_SECTORS 32768u
static uint8_t g_disk[DISK_SECTORS * 512u];
static int g_writes;
static int g_fail_after = -1;

void host_disk_reset(void) { memset(g_disk, 0, sizeof(g_disk)); g_writes = 0; g_fail_after = -1; }
int  host_disk_writes(void) { return g_writes; }
void host_disk_fail_after(int n) { g_fail_after = n; }

int host_disk_save(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(g_disk, 1, sizeof(g_disk), f);
    fclose(f);
    return n == sizeof(g_disk) ? 0 : -1;
}
int host_disk_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(g_disk, 1, sizeof(g_disk), f);
    fclose(f);
    return n == sizeof(g_disk) ? 0 : -1;
}

int virtio_blk_read(uint64_t sector, void *buf)
{
    if (sector >= DISK_SECTORS) return -1;
    memcpy(buf, g_disk + sector * 512u, 512);
    return 0;
}
int virtio_blk_write(uint64_t sector, const void *buf)
{
    if (sector >= DISK_SECTORS) return -1;
    if (g_fail_after == 0) return -1;
    if (g_fail_after > 0) g_fail_after--;
    memcpy(g_disk + sector * 512u, buf, 512);
    g_writes++;
    return 0;
}

void kmutex_init(struct kmutex *m, const char *name) { (void)m; (void)name; }
void kmutex_lock(struct kmutex *m) { (void)m; }
void kmutex_unlock(struct kmutex *m) { (void)m; }

void rtc_get_datetime(struct rtc_time *t)
{
    t->year = 2026; t->month = 11; t->day = 3; t->hour = 12; t->min = 0; t->sec = 0;
}
int  arp_format_cache(char *out, uint32_t max) { if (max) out[0] = 0; return 0; }
void virtio_net_stats(uint32_t *a, uint32_t *b, uint64_t *c, uint64_t *d) { *a = *b = 0; *c = *d = 0; }
int  image_create_demo_bmp(uint8_t *o, size_t m, uint32_t w, uint32_t h) { (void)o;(void)m;(void)w;(void)h; return 0; }
int  image_create_chip_bmp(uint8_t *o, size_t m, uint32_t w, uint32_t h) { (void)o;(void)m;(void)w;(void)h; return 0; }
