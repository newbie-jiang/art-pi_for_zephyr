#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <ff.h>
#include <string.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* ===== 配置区 ===== */
#define MNT_POINT       "/SD:"           /* FATFS 盘符 */
#define STORAGE_DEV     "SD"             /* SDMMC/SPI-SD 注册的磁盘名 */
#define TEST_FILE       MNT_POINT "/fatfs_bench.bin"

#ifndef BENCH_SIZE_MIB
#define BENCH_SIZE_MIB  64              /* 测试总量（MiB） */
#endif
#ifndef CHUNK_SIZE
#define CHUNK_SIZE      (128 * 1024)     /* 块大小，建议 256 KiB~1 MiB */
#endif

/* 预分配模式：0=不预分配；1=零写一遍预分配（稳，但多花同等时间） */
#ifndef PREALLOC_MODE
#define PREALLOC_MODE   0
#endif

/* ===== FATFS 挂载结构 ===== */
static FATFS fat_fs;
static struct fs_mount_t mp = {
    .type        = FS_FATFS,
    .mnt_point   = MNT_POINT,
    .fs_data     = &fat_fs,
    .storage_dev = (void *)STORAGE_DEV,
};

/* ===== 用毫秒计算的 MB/s（*100）打印，和墙钟一致 ===== */
static void print_mbps_x100_ms(const char *tag, size_t bytes, uint64_t elapsed_ms)
{
    if (elapsed_ms == 0) elapsed_ms = 1; /* 防止除零 */
    /* MB/s = bytes / (ms/1000) / (1024*1024) */
    /* 等价：bytes * 100 * 1000 / (ms * 1024 * 1024) */
    uint64_t num = (uint64_t)bytes * 100ULL * 1000ULL;
    uint64_t den = (uint64_t)elapsed_ms * 1024ULL * 1024ULL;
    uint64_t mbps_x100 = num / den;

    printk("%s: %u bytes in %llu ms => %llu.%02llu MB/s\n",
           tag, (unsigned)bytes,
           (unsigned long long)elapsed_ms,
           mbps_x100 / 100, mbps_x100 % 100);
}

/* 可选：零写一遍做“预分配”（避免 lseek 扩容导致 -22） */
static int preallocate_by_zero_fill(const char *path, size_t total_bytes)
{
    static uint8_t __aligned(32) zbuf[CHUNK_SIZE];
    memset(zbuf, 0, sizeof(zbuf));

    struct fs_file_t f;
    fs_file_t_init(&f);
    int rc = fs_open(&f, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
    if (rc) return rc;

    size_t done = 0;
    while (done < total_bytes) {
        size_t n = MIN(sizeof(zbuf), total_bytes - done);
        rc = fs_write(&f, zbuf, n);
        if (rc < 0) {
            fs_close(&f);
            return rc;
        }
        done += (size_t)rc;
    }
    fs_sync(&f);
    fs_close(&f);
    return 0;
}

/* ===== 写吞吐测试：顺序写 N MiB ===== */
static int bench_write(const char *path, size_t total_bytes, uint64_t *elapsed_ms)
{
    static uint8_t __aligned(32) buf[CHUNK_SIZE];
    for (size_t i = 0; i < sizeof(buf); i++) buf[i] = (uint8_t)i;

    fs_unlink(path);  /* 忽略 ENOENT */

#if PREALLOC_MODE == 1
    /* 用零写“预分配”，更稳：避免 FAT 频繁扩展带来的抖动 */
    int rc0 = preallocate_by_zero_fill(path, total_bytes);
    if (rc0 < 0) {
        LOG_WRN("preallocate failed rc=%d, continue without it", rc0);
        fs_unlink(path);
    }
#endif

    struct fs_file_t f;
    fs_file_t_init(&f);

    int rc = fs_open(&f, path, FS_O_CREATE | FS_O_WRITE | FS_O_TRUNC);
    if (rc) {
        LOG_ERR("open write rc=%d", rc);
        return rc;
    }

    size_t written = 0;
    int64_t t0 = k_uptime_get();

    while (written < total_bytes) {
        size_t n = MIN((size_t)sizeof(buf), total_bytes - written);
        int w = fs_write(&f, buf, n);
        if (w < 0) {
            LOG_ERR("write rc=%d", w);
            fs_close(&f);
            return w;
        }
        written += (size_t)w;
    }

    fs_sync(&f);
    int64_t t1 = k_uptime_get();
    fs_close(&f);

    *elapsed_ms = (uint64_t)(t1 - t0);
    print_mbps_x100_ms("WRITE (file)", written, *elapsed_ms);
    return 0;
}

/* ===== 读吞吐测试：顺序读 N MiB ===== */
static int bench_read(const char *path, size_t total_bytes, uint64_t *elapsed_ms)
{
    static uint8_t __aligned(32) buf[CHUNK_SIZE];
    struct fs_file_t f;
    fs_file_t_init(&f);

    int rc = fs_open(&f, path, FS_O_READ);
    if (rc) {
        LOG_ERR("open read rc=%d", rc);
        return rc;
    }

    size_t read_total = 0;
    uint32_t sink = 0;

    int64_t t0 = k_uptime_get();

    while (read_total < total_bytes) {
        size_t n = MIN((size_t)sizeof(buf), total_bytes - read_total);
        int r = fs_read(&f, buf, n);
        if (r < 0) {
            LOG_ERR("read rc=%d", r);
            fs_close(&f);
            return r;
        }
        if (r == 0) break; /* EOF */
        for (int i = 0; i < r; i++) sink += buf[i]; /* 防优化 */
        read_total += (size_t)r;
    }

    int64_t t1 = k_uptime_get();
    fs_close(&f);

    *elapsed_ms = (uint64_t)(t1 - t0);
    print_mbps_x100_ms("READ (file)", read_total, *elapsed_ms);
    printk("READ sink=0x%08x\n", sink);
    return 0;
}

void main(void)
{
    const size_t total_bytes = (size_t)BENCH_SIZE_MIB * 1024U * 1024U;
    int rc;

    k_sleep(K_MSEC(300)); /* 给卡上电 */

    rc = fs_mount(&mp);
    if (rc) {
        LOG_ERR("fs_mount(%s) failed: %d", MNT_POINT, rc);
        return;
    }
    LOG_INF("Mounted %s", MNT_POINT);

    printk("\n=== FATFS throughput bench: %u MiB, chunk %u B ===\n",
           (unsigned)BENCH_SIZE_MIB, (unsigned)CHUNK_SIZE);

    uint64_t ms_w=0, ms_r=0;

    /* 写测试（用墙钟计时） */
    rc = bench_write(TEST_FILE, total_bytes, &ms_w);
    if (rc) goto out;

    /* 避免读命中文件缓存：卸载→再挂载 */
    fs_unmount(&mp);
    rc = fs_mount(&mp);
    if (rc) {
        LOG_ERR("remount failed: %d", rc);
        return;
    }

    /* 读测试（用墙钟计时） */
    rc = bench_read(TEST_FILE, total_bytes, &ms_r);
    if (rc) goto out;

out:
    fs_unmount(&mp);
    LOG_INF("Done.");
}
