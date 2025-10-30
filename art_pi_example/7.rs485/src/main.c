/*
 * SPDX-License-Identifier: Apache-2.0
 * RS-485 半双工测试：UART5 (115200-8-N-1) + 方向脚
 * - 每秒发送 "PING n\r\n"
 * - 收到什么就回显出去
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <stdio.h>

#define UART_DEV   DEVICE_DT_GET(DT_ALIAS(rs485))          /* &uart5 */
#define DIR_NODE   DT_ALIAS(rs485de)                       /* 方向脚别名节点 */
static const struct gpio_dt_spec rs485_dir =
        GPIO_DT_SPEC_GET(DIR_NODE, gpios);

/* 根据设备树极性定义：1=发，0=收（ACTIVE_HIGH 时就是这样；若 ACTIVE_LOW，驱动会自动反相） */
static inline void rs485_set_tx(void) { gpio_pin_set_dt(&rs485_dir, 1); }
static inline void rs485_set_rx(void) { gpio_pin_set_dt(&rs485_dir, 0); }

/* 发送一段缓冲（带方向切换与尾字节等待） */
static void rs485_send_buf(const struct device *uart, const uint8_t *buf, size_t len)
{
    rs485_set_tx();
    /* 等待半个字节时间作为总线“转向”保护：~50–100us 即可 */
    k_busy_wait(100);

    for (size_t i = 0; i < len; i++) {
        uart_poll_out(uart, buf[i]);
    }
    /* 115200 下 1 字节 ~87us，多等一点确保最后一个字节发完再切回接收 */
    k_msleep(2);
    rs485_set_rx();
}

void main(void)
{
    const struct device *uart = UART_DEV;
    if (!device_is_ready(uart) || !device_is_ready(rs485_dir.port)) {
        return;
    }

    /* 配置方向脚为输出（先切到接收态） */
    if (gpio_pin_configure_dt(&rs485_dir, GPIO_OUTPUT_INACTIVE) != 0) {
        return;
    }
    rs485_set_rx();

    /* 配置 UART：115200-8-N-1，无硬件流控 */
    struct uart_config cfg = {
        .baudrate  = 115200,
        .parity    = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .data_bits = UART_CFG_DATA_BITS_8,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
    };
    (void)uart_configure(uart, &cfg);

    /* 欢迎信息 */
    const char *hello = "=== RS485 test on UART5 (115200-8-N-1) ===\r\n";
    rs485_send_buf(uart, (const uint8_t *)hello, strlen(hello));

    /* 主循环：轮询收 + 周期发 PING */
    uint8_t rx;
    uint32_t seq = 0;
    int64_t last_ping = k_uptime_get();

    while (1) {
        /* 收到就回显 */
        while (uart_poll_in(uart, &rx) == 0) {
            rs485_send_buf(uart, &rx, 1);
        }

        /* 每 1 秒主动发一行 PING */
        int64_t now = k_uptime_get();
        if (now - last_ping >= 1000) {
            last_ping = now;
            char line[40];
            int n = snprintk(line, sizeof(line), "PING %lu\r\n", (unsigned long)seq++);
            rs485_send_buf(uart, (const uint8_t *)line, (size_t)n);
        }

        k_msleep(1);
    }
}
