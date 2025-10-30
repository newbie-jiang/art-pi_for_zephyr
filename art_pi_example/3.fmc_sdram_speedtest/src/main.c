#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>

/* ---------- LED 别名，沿用 aliases ---------- */
#define LED0_NODE DT_ALIAS(led0)  /* red_led  */
#define LED1_NODE DT_ALIAS(led1)  /* blue_led */

static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);

/* ---------- 选择 SDRAM 节点：优先 sdram1，否则 sdram2 ---------- */
#if DT_NODE_EXISTS(DT_NODELABEL(sdram1))
#define SDRAM_NODE DT_NODELABEL(sdram1)
#elif DT_NODE_EXISTS(DT_NODELABEL(sdram2))
#define SDRAM_NODE DT_NODELABEL(sdram2)
#else
#warning "No SDRAM node (sdram1/sdram2) in devicetree!"
#endif

#ifdef SDRAM_NODE
#define SDRAM_BASE  DT_REG_ADDR(SDRAM_NODE)
#define SDRAM_SIZE  DT_REG_SIZE(SDRAM_NODE)
#endif

/* 测试容量（字节）。默认 4 MiB，避免久等；稳定后可改为 SDRAM_SIZE */
#ifndef SDRAM_TEST_BYTES
#define SDRAM_TEST_BYTES  (4 * 1024 * 1024UL)
// #define SDRAM_TEST_BYTES  SDRAM_SIZE
#endif


/**
 * @brief  SDRAM 快速自检（Quick Sanity Test）
 *
 * 思路：
 *  1) 选定一段 SDRAM 测试窗口（默认 SDRAM_TEST_BYTES，且不超过设备树申明的 SDRAM_SIZE）。
 *  2) 依次写入并校验三种花纹：
 *     - Pass1: 0xAAAAAAAA（检出某些位“粘 0/1”、耦合问题）
 *     - Pass2: 0x55555555（与 Pass1 互补）
 *     - Pass3: 地址(索引)花纹 i（更容易检出地址线/行列位宽/位交换错误）
 *
 * 前置条件：
 *  - 设备树存在 SDRAM 节点（sdram1/sdram2），且 FMC/MEMC 已初始化；
 *  - SDRAM_BASE / SDRAM_SIZE 从设备树导出；
 *  - 若 SDRAM MPU 属性是可缓存(Cachable)，更严格测试前可在“写后、读前”
 *    使用 sys_cache_data_flush_range / sys_cache_data_invd_range 以避免命中缓存。
 *
 * 返回：
 *  - 0        : 所有校验通过
 *  - -ENODEV  : 无 SDRAM 节点
 *  - -EIO     : 任何一次比对失败（会打印失败地址与期望/实际值）
 */
static int sdram_quick_test(void)
{
#ifndef SDRAM_NODE
    /* 没有在设备树里找到 SDRAM 节点，直接跳过测试 */
    printk("No SDRAM node, skip SDRAM test.\n");
    return -ENODEV;
#else
    /* 使用 volatile 确保每次读写都走总线，不被编译器优化掉 */
    volatile uint32_t *p = (volatile uint32_t *)SDRAM_BASE;

    /* 计算本次测试的总字节数：不超过配置上限，也不超过物理容量 */
    size_t total = MIN((size_t)SDRAM_TEST_BYTES, (size_t)SDRAM_SIZE);

    /* 以 32 位为步长进行测试 */
    size_t words = total / sizeof(uint32_t);

    uint32_t val;
    size_t i;

    printk("SDRAM test begin: base=0x%08lx size=%lu bytes (test=%lu)\n",
           (unsigned long)SDRAM_BASE, (unsigned long)SDRAM_SIZE, (unsigned long)total);

    /* -------- Pass 1: 固定花纹 0xAAAAAAAA，先写满 -------- */
    for (i = 0; i < words; i++) {
        p[i] = 0xAAAAAAAAu;
    }
    /* 再逐个读回并比对，任何不一致立刻报错返回 */
    for (i = 0; i < words; i++) {
        val = p[i];
        if (val != 0xAAAAAAAAu) {
            printk("SDRAM FAIL P1 @0x%08lx exp=0x%08x got=0x%08x (i=%lu)\n",
                   (unsigned long)((uintptr_t)&p[i]), 0xAAAAAAAAu, val, (unsigned long)i);
            return -EIO;
        }
    }

    /* -------- Pass 2: 固定花纹 0x55555555（与 Pass1 互补） -------- */
    for (i = 0; i < words; i++) {
        p[i] = 0x55555555u;
    }
    for (i = 0; i < words; i++) {
        val = p[i];
        if (val != 0x55555555u) {
            printk("SDRAM FAIL P2 @0x%08lx exp=0x%08x got=0x%08x (i=%lu)\n",
                   (unsigned long)((uintptr_t)&p[i]), 0x55555555u, val, (unsigned long)i);
            return -EIO;
        }
    }

    /* -------- Pass 3: 地址/索引花纹（定位地址线/位宽配置问题更敏感） -------- */
    for (i = 0; i < words; i++) {
        p[i] = (uint32_t)i;  /* 将索引值写入对应地址 */
    }

    for (i = 0; i < words; i++) {
        val = p[i];
        if (val != (uint32_t)i) {
            /* 打印失败的“物理地址指针”、期望值与实际值，方便定位哪一段出错 */
            printk("SDRAM FAIL P3 @0x%08lx exp=0x%08lx got=0x%08lx (i=%lu)\n",
                   (unsigned long)((uintptr_t)&p[i]),
                   (unsigned long)i, (unsigned long)val, (unsigned long)i);
            return -EIO;
        }
    }

    /* 三轮全都一致，说明在当前频率/时序下基本可用 */
    printk("SDRAM test PASS ✅ (%lu bytes checked)\n", (unsigned long)total);
    return 0;
#endif
}





#include <zephyr/cache.h>
#include <zephyr/sys_clock.h>

#ifndef SDRAM_BENCH_BYTES
/* 默认跑 8 MiB；根据你的 SDRAM 容量/串口速度调小/调大 */
#define SDRAM_BENCH_BYTES (4U * 1024U * 1024U)
#endif

/* 内部 SRAM 的分块缓冲，用于 memcpy 往返测试。
 * 放 .bss 即可，默认会在片内 SRAM（非 SDRAM）。
 */
#define CHUNK_SIZE (64U * 1024U)
static uint8_t sram_chunk[CHUNK_SIZE] __aligned(32);

static inline double cycles_to_seconds(uint32_t cycles)
{
    return (double)cycles / (double)sys_clock_hw_cycles_per_sec();
}

/* 可选：强制内存屏障，保证访存次序 */
static inline void mem_barrier(void)
{
#if defined(__ARM_ARCH)
    __DSB(); __ISB();
#endif
}

/* 简单吞吐打印：bytes / time = MB/s */
static void print_mbps(const char *tag, size_t bytes, uint32_t cycles)
{
    double sec = cycles_to_seconds(cycles);
    double mbps = (bytes / (1024.0 * 1024.0)) / (sec > 0 ? sec : 1e-9);
    printk("%s: %u bytes in %u cycles (%.3f s) => %.2f MB/s\n",
           tag, (unsigned)bytes, (unsigned)cycles, sec, mbps);
}

/* 核心测速 */
static void sdram_bench(void)
{
#ifndef SDRAM_NODE
    printk("No SDRAM node, skip bench.\n");
    return;
#else
    volatile uint32_t *p = (volatile uint32_t *)SDRAM_BASE;
    size_t max_bytes = MIN((size_t)SDRAM_BENCH_BYTES, (size_t)SDRAM_SIZE);
    size_t words     = max_bytes / sizeof(uint32_t);
    uint32_t i;

    printk("\n=== SDRAM bandwidth bench ===\n");
    printk("window: base=0x%08lx size=%lu bytes (test=%lu)\n",
           (unsigned long)SDRAM_BASE, (unsigned long)SDRAM_SIZE, (unsigned long)max_bytes);

    /* -------- Write (CPU -> SDRAM) -------- */
    {
        /* 失效目标范围（有些架构不是必须，这里稳妥起见） */
        sys_cache_data_invd_range((void *)SDRAM_BASE, max_bytes);
        mem_barrier();

        uint32_t t0 = k_cycle_get_32();
        for (i = 0; i < words; i++) {
            /* 32-bit streaming 写，花纹随 i 变，降低 store buffer 合并影响 */
            p[i] = 0xA5A50000u ^ i;
        }
        mem_barrier();
        /* 写回（若是 write-back cache，可以确保总线已刷出） */
        sys_cache_data_flush_range((void *)SDRAM_BASE, max_bytes);

        uint32_t dt = k_cycle_get_32() - t0;
        print_mbps("WRITE 32-bit (CPU->SDRAM)", max_bytes, dt);
    }

    /* -------- Read (SDRAM -> CPU) -------- */
    {
        /* 读前先失效数据 cache，确保从外设总线真正取数 */
        sys_cache_data_invd_range((void *)SDRAM_BASE, max_bytes);
        mem_barrier();

        volatile uint32_t sink = 0; /* 防优化累加器 */
        uint32_t t0 = k_cycle_get_32();
        for (i = 0; i < words; i++) {
            sink += p[i];
        }
        mem_barrier();
        uint32_t dt = k_cycle_get_32() - t0;
        print_mbps("READ 32-bit (SDRAM->CPU)", max_bytes, dt);

        /* 防止编译器删掉循环 */
        printk("READ sink=0x%08lx\n", (unsigned long)sink);
    }

    /* -------- memcpy: SDRAM -> SRAM (chunked) -------- */
    {
        size_t done = 0;
        sys_cache_data_invd_range((void *)SDRAM_BASE, max_bytes);
        mem_barrier();

        uint32_t t0 = k_cycle_get_32();
        while (done < max_bytes) {
            size_t n = MIN(CHUNK_SIZE, max_bytes - done);
            /* 失效本次源块，确保从 SDRAM 读 */
            sys_cache_data_invd_range((void *)(SDRAM_BASE + done), n);
            memcpy(sram_chunk, (void *)(SDRAM_BASE + done), n);
            done += n;
        }
        mem_barrier();
        uint32_t dt = k_cycle_get_32() - t0;
        print_mbps("memcpy SDRAM->SRAM", max_bytes, dt);
    }

    /* -------- memcpy: SRAM -> SDRAM (chunked) -------- */
    {
        /* 先把 SRAM 块填充点数据 */
        for (size_t k = 0; k < CHUNK_SIZE; k += 4) {
            ((uint32_t *)sram_chunk)[k / 4] = 0x5A5A0000u ^ (uint32_t)k;
        }

        size_t done = 0;
        uint32_t t0 = k_cycle_get_32();
        while (done < max_bytes) {
            size_t n = MIN(CHUNK_SIZE, max_bytes - done);
            memcpy((void *)(SDRAM_BASE + done), sram_chunk, n);
            /* 写回刚拷贝到 SDRAM 的缓存块 */
            sys_cache_data_flush_range((void *)(SDRAM_BASE + done), n);
            done += n;
        }
        mem_barrier();
        uint32_t dt = k_cycle_get_32() - t0;
        print_mbps("memcpy SRAM->SDRAM", max_bytes, dt);
    }

    printk("=== SDRAM bench done ===\n\n");
#endif
}





static int leds_init(void)
{
    if (!device_is_ready(led0.port) || !device_is_ready(led1.port)) {
        printk("LED ports not ready\n");
        return -ENODEV;
    }
    int rc = gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
    if (rc) return rc;
    rc = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
    return rc;
}


int main(void)
{
    /* 关掉 stdio 缓冲，printf 立即可见（可选） */
    setvbuf(stdout, NULL, _IONBF, 0);

    printk("=== Board boot ===\n");
#ifdef SDRAM_NODE
    printk("Using SDRAM node: %s, base=0x%08lx size=%lu\n",
           DT_NODE_FULL_NAME(SDRAM_NODE),
           (unsigned long)SDRAM_BASE, (unsigned long)SDRAM_SIZE);
#endif

    if (leds_init() != 0) {
        return 0;
    }

    /* SDRAM 快测 */
    int s = sdram_quick_test();
    if (s == 0) {
        /* 测试通过，亮蓝灯提示 */
        gpio_pin_set_dt(&led1, 1);
    } else {
        /* 测试失败，红灯常亮并停机 */
        gpio_pin_set_dt(&led0, 1);
        printk("Stop due to SDRAM error.\n");
        while (1) { k_sleep(K_FOREVER); }
    }

    /* sdrm 读写测试 */
     sdram_bench();


    /* 循环：每秒交替闪烁两灯 */
    while (1) {
        gpio_pin_toggle_dt(&led0);
        gpio_pin_toggle_dt(&led1);
        // printf("LEDs toggled\n");
        k_msleep(1000);
    }
}
