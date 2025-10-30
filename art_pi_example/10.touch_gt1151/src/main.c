
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>
#include <stdbool.h>

/* ----------- LED (容错) ----------- */
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET_OR(LED0_NODE, gpios, (struct gpio_dt_spec){0});
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET_OR(LED1_NODE, gpios, (struct gpio_dt_spec){0});

static void leds_toggle(void) {
    if (led0.port) gpio_pin_toggle_dt(&led0);
    if (led1.port) gpio_pin_toggle_dt(&led1);
}
static void leds_init(void) {
    if (led0.port && device_is_ready(led0.port)) gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
    if (led1.port && device_is_ready(led1.port)) gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
}

/* ----------- 分辨率：chosen display 优先 ----------- */
#if DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_display), okay) && \
    DT_NODE_HAS_PROP(DT_CHOSEN(zephyr_display), width) && \
    DT_NODE_HAS_PROP(DT_CHOSEN(zephyr_display), height)
#define TOUCH_W DT_PROP(DT_CHOSEN(zephyr_display), width)
#define TOUCH_H DT_PROP(DT_CHOSEN(zephyr_display), height)
#else
#define TOUCH_W 800
#define TOUCH_H 480
#endif

/* ----------- 最大触点 ----------- */
#ifdef CONFIG_INPUT_GT1151_MAX_TOUCH_POINTS
#define MAX_SLOTS CONFIG_INPUT_GT1151_MAX_TOUCH_POINTS
#else
#define MAX_SLOTS 5
#endif

/* ----------- 仅监听 GT1151；无实例时退化为全局监听 ----------- */
#if DT_HAS_COMPAT_STATUS_OKAY(goodix_gt1151)
#define HAVE_GT1151 1
#define GT_DEV DEVICE_DT_GET(DT_INST(0, goodix_gt1151))
#else
#define HAVE_GT1151 0
#endif

/* ----------- 触点状态缓存（回调里只更新，不打印） ----------- */
struct slot_state {
    int16_t x, y;
    bool down;
    bool dirty;   /* 本周期有更新 */
};
static struct slot_state slots[MAX_SLOTS];
static int current_slot = 0;

/* 简单限幅 */
static inline int16_t clamp16(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return (int16_t)v;
}

/* 回调：只更新状态，标记脏位，不打印 */
static void touch_cb(struct input_event *evt, void *user_data)
{
    ARG_UNUSED(user_data);

    switch (evt->type) {
    case INPUT_EV_ABS:
        if (evt->code == INPUT_ABS_MT_SLOT) {
            int s = (int)evt->value;
            if (s < 0) s = 0;
            if (s >= MAX_SLOTS) s = MAX_SLOTS - 1;
            current_slot = s;
        } else if (evt->code == INPUT_ABS_X) {
            slots[current_slot].x = clamp16(evt->value, 0, TOUCH_W - 1);
            slots[current_slot].dirty = true;
        } else if (evt->code == INPUT_ABS_Y) {
            slots[current_slot].y = clamp16(evt->value, 0, TOUCH_H - 1);
            slots[current_slot].dirty = true;
        }
        break;

    case INPUT_EV_KEY:
        if (evt->code == INPUT_BTN_TOUCH) {
            bool d = (evt->value != 0);
            if (slots[current_slot].down != d) {
                slots[current_slot].down = d;
                slots[current_slot].dirty = true;
            }
        }
        break;

    default:
        break;
    }
}

/* 有 GT1151 就只绑定该设备，否则绑定所有设备 */
#if HAVE_GT1151
INPUT_CALLBACK_DEFINE(GT_DEV, touch_cb, NULL);
#else
INPUT_CALLBACK_DEFINE(NULL,   touch_cb, NULL);
#endif

/* 定时器：固定节流频率打印（比如 50ms 一次） */
#define PRINT_PERIOD_MS 50
static struct k_timer print_timer;

static void print_timer_cb(struct k_timer *tmr)
{
    /* 一次性打印所有“脏”触点，然后清脏位 */
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!slots[i].dirty) continue;
        if (slots[i].down) {
            printk("[slot %d] DOWN/MOVE x=%d y=%d\n", i, slots[i].x, slots[i].y);
        } else {
            printk("[slot %d] UP\n", i);
        }
        slots[i].dirty = false;
    }
    leds_toggle();
}

void main(void)
{
    leds_init();

#if HAVE_GT1151
    const struct device *dev = GT_DEV;
    if (!device_is_ready(dev)) {
        printk("Touch dev not ready: %s\n", dev->name);
    } else {
        printk("Listening GT115x dev: %s, resolution %dx%d\n",
               dev->name ? dev->name : "?", TOUCH_W, TOUCH_H);
    }
#else
    printk("Listening ALL input devices, resolution %dx%d\n", TOUCH_W, TOUCH_H);
#endif

    k_timer_init(&print_timer, print_timer_cb, NULL);
    k_timer_start(&print_timer, K_MSEC(PRINT_PERIOD_MS), K_MSEC(PRINT_PERIOD_MS));

    /* 主线程空转 */
    while (1) {
        k_sleep(K_SECONDS(1));
    }
}


/*  非驱动层的测试  */

// #include <zephyr/kernel.h>
// #include <zephyr/sys/printk.h>
// #include <zephyr/drivers/i2c.h>
// #include <zephyr/drivers/gpio.h>
// #include <zephyr/sys/byteorder.h>
// #include <zephyr/sys/util.h>

// /* ------- Devicetree 节点抓取（兼容 nodelabel/alias） ------- */
// #if DT_NODE_HAS_STATUS(DT_NODELABEL(touch), okay)
// #define TOUCH_NODE DT_NODELABEL(touch)
// #elif DT_NODE_HAS_STATUS(DT_ALIAS(touch), okay)
// #define TOUCH_NODE DT_ALIAS(touch)
// #else
// #error "No touch node found: please add `touch:` label or alias in DTS"
// #endif

// /* I2C 与 GPIO 描述（来自 touch 节点） */
// static const struct i2c_dt_spec  i2c_ts  = I2C_DT_SPEC_GET(TOUCH_NODE);
// static const struct gpio_dt_spec pin_int = GPIO_DT_SPEC_GET(TOUCH_NODE, irq_gpios);
// static const struct gpio_dt_spec pin_rst = GPIO_DT_SPEC_GET(TOUCH_NODE, reset_gpios);

// /* ---- 常用寄存器/参数（GT115x） ---- */
// #define GT_ADDR7             0x14
// #define GT_REG_PID           0x8140  /* 4 bytes */
// #define GT_REG_REQ           0x8044  /* Request */
// #define GT_REG_STATUS        0x814E  /* bit7: ready, bit3..0: points */
// #define GT_REG_POINTS_START  0x8150  /* 每点 8 字节: xL,xH,yL,yH,sizeL,sizeH,id,event */
// #define GT_POINT_SIZE        8
// #define GT_MAX_POINTS        5

// /* ---- I2C 读写（两段消息，失败回退 write_read） ---- */
// static int gt_read(uint8_t addr7, uint16_t reg, void *buf, size_t len)
// {
//     uint8_t ra[2]; sys_put_be16(reg, ra);
//     struct i2c_msg msgs[2] = {
//         { .buf = ra,  .len = 2,   .flags = I2C_MSG_WRITE },
//         { .buf = buf, .len = len, .flags = I2C_MSG_READ | I2C_MSG_RESTART | I2C_MSG_STOP },
//     };
//     int ret = i2c_transfer(i2c_ts.bus, msgs, 2, addr7);
//     if (ret) {
//         /* fallback：部分控制器/设备对 RESTART 更挑剔 */
//         ret = i2c_write_read(i2c_ts.bus, addr7, ra, 2, buf, len);
//     }
//     return ret;
// }

// static int gt_write_u8(uint8_t addr7, uint16_t reg, uint8_t val)
// {
//     uint8_t frame[3];
//     sys_put_be16(reg, frame);
//     frame[2] = val;
//     return i2c_write(i2c_ts.bus, frame, sizeof(frame), addr7);
// }

// /* ---- 复位/上电序列（按 ACTIVE 语义；你的 RST=GPIO_ACTIVE_LOW） ----
//  * 断言复位(=ACTIVE → 对低有效就是拉低) ≥10ms → 释放(=INACTIVE) → 等 ≥60ms
//  * INT：上电阶段先拉为输出低（多数板卡 OK），随后切回输入上拉。
//  */
// static int gt_power_on_sequence(void)
// {
//     int ret;

//     if (!device_is_ready(i2c_ts.bus) ||
//         !gpio_is_ready_dt(&pin_int) ||
//         !gpio_is_ready_dt(&pin_rst)) {
//         return -ENODEV;
//     }

//     /* INT 先设为输出低用于上电阶段（如需绑位），RST 受控 */
//     ret = gpio_pin_configure_dt(&pin_int, GPIO_OUTPUT);
//     if (ret) return ret;
//     ret = gpio_pin_configure_dt(&pin_rst, GPIO_OUTPUT_INACTIVE); /* 先置为“无效”电平 */
//     if (ret) return ret;

//     gpio_pin_set_dt(&pin_int, 0);   /* INT=低（若外部有上拉，后面会切回输入） */

//     /* 断言复位（ACTIVE=1 → 对低有效脚就是拉低） */
//     gpio_pin_set_dt(&pin_rst, 1);
//     k_msleep(12);                   /* ≥10ms */

//     /* 释放复位（INACTIVE=0 → 对低有效脚就是拉高） */
//     gpio_pin_set_dt(&pin_rst, 0);

//     /* INT 切回输入（外部/DT 上拉） */
//     ret = gpio_pin_configure_dt(&pin_int, GPIO_INPUT);
//     if (ret) return ret;

//     /* 给固件启动时间（≥60ms） */
//     k_msleep(60);
//     return 0;
// }

// /* 可重复调用的复位（与上电一致） */
// static int gt_reset_sequence(void)
// {
//     return gt_power_on_sequence();
// }

// /* ---- Request 处理：无配置时先清零继续 ---- */
// static void gt_handle_request(void)
// {
//     uint8_t req = 0xFF;
//     if (gt_read(GT_ADDR7, GT_REG_REQ, &req, 1) == 0) {
//         if (req == 0x01) {
//             printk("GT Request=0x01: device asks for config; clear and continue.\n");
//             gt_write_u8(GT_ADDR7, GT_REG_REQ, 0x00);
//         } else if (req == 0x03) {
//             printk("GT Request=0x03: device asks for reset, doing reset.\n");
//             gt_write_u8(GT_ADDR7, GT_REG_REQ, 0x00);
//             gt_reset_sequence();
//         }
//     }
// }

// /* ---- 读取 PID ---- */
// static int gt_read_pid(char out[5])
// {
//     uint8_t id[4] = {0};
//     int ret = gt_read(GT_ADDR7, GT_REG_PID, id, 4);
//     if (ret) return ret;
//     out[0] = id[0]; out[1] = id[1]; out[2] = id[2]; out[3] = id[3]; out[4] = '\0';
//     return 0;
// }

// /* ---- 简单轮询：先读 0x814E → 读 n*8 点 → 清 0x814E ---- */
// static void gt_poll_points(void)
// {
//     while (1) {
//         uint8_t stat = 0;
//         if (gt_read(GT_ADDR7, GT_REG_STATUS, &stat, 1) == 0) {
//             if (stat & 0x80) { /* bit7 ready */
//                 uint8_t n = stat & 0x0F;
//                 if (n > GT_MAX_POINTS) n = GT_MAX_POINTS;

//                 if (n) {
//                     uint8_t buf[GT_MAX_POINTS * GT_POINT_SIZE] = {0};
//                     size_t need = n * GT_POINT_SIZE;

//                     if (gt_read(GT_ADDR7, GT_REG_POINTS_START, buf, need) == 0) {
//                         for (uint8_t i = 0; i < n; i++) {
//                             const uint8_t *p = &buf[i * GT_POINT_SIZE];
//                             uint16_t x = ((uint16_t)p[1] << 8) | p[0];
//                             uint16_t y = ((uint16_t)p[3] << 8) | p[2];
//                             uint16_t s = ((uint16_t)p[5] << 8) | p[4]; /* size/pressure */
//                             uint8_t  id = p[6];                        /* track id */
//                             uint8_t  ev = p[7];                        /* event */
//                             printk("pt[%u]: id=%u ev=%u  x=%u y=%u s=%u\n",
//                                    i, id, ev, x, y, s);
//                         }
//                     }
//                 }
//                 /* 清空 ready 标志（读取完成必须清零） */
//                 gt_write_u8(GT_ADDR7, GT_REG_STATUS, 0x00);
//             }
//         }
//         k_msleep(10);
//     }
// }

// #ifdef CONFIG_APP_I2C_SCAN
// static void i2c_bus_scan(const struct device *dev)
// {
//     printk("I2C scan on %s:\n", dev->name);
//     for (uint8_t a = 0x03; a < 0x78; a++) {
//         int ret = i2c_write(dev, NULL, 0, a);
//         if (ret == 0) {
//             printk("  - found addr 0x%02X\n", a);
//         }
//     }
// }
// #endif

// void main(void)
// {
//     printk("*** GT1151/GT1158 bring-up test (addr 0x14) ***\n");

//     if (!device_is_ready(i2c_ts.bus) ||
//         !gpio_is_ready_dt(&pin_int) ||
//         !gpio_is_ready_dt(&pin_rst)) {
//         printk("Devices not ready\n");
//         return;
//     }

// #ifdef CONFIG_APP_I2C_SCAN
//     printk("I2C bus = %s  addr from DTS=0x%02X\n", i2c_ts.bus->name, i2c_ts.addr);
//     i2c_bus_scan(i2c_ts.bus);
// #endif

//     if (gt_power_on_sequence()) {
//         printk("power-on sequence failed\n");
//         return;
//     }

//     char pid[5] = {0};
//     if (gt_read_pid(pid) == 0) {
//         printk("PID=\"%s\"\n", pid);   /* 你这里大概率会看到 “1158” */
//     } else {
//         printk("Read PID failed\n");
//         return;
//     }

//     gt_handle_request();
//     printk("Ready. Polling points...\n");
//     gt_poll_points();
// }


