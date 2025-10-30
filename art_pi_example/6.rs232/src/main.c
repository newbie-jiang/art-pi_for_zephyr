/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * RS232 <-> USB 适配器测试
 * - USART6: 115200-8-N-1, 无流控
 * - 每秒发送一行 "PING n"
 * - 将接收到的字节原样回显
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include <stdio.h>

#define UART_NODE DT_NODELABEL(usart6)   /* 就用 usart6 节点 */

static const struct device *uart;

/* 简单发送字符串（附带 CRLF 兼容串口工具） */
static void uart_puts(const struct device *dev, const char *s)
{
	for (const char *p = s; *p; ++p) {
		uart_poll_out(dev, (unsigned char)*p);
	}
}

/* 发送一行，并自动结尾 \r\n */
static void uart_putsln(const struct device *dev, const char *s)
{
	uart_puts(dev, s);
	uart_puts(dev, "\r\n");
}

void main(void)
{
	uart = DEVICE_DT_GET(UART_NODE);
	if (!device_is_ready(uart)) {
		return;
	}

	/* 配置 115200-8-N-1，无硬件流控 */
	struct uart_config cfg = {
		.baudrate = 115200,
		.parity = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_1,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};
	(void)uart_configure(uart, &cfg); /* 大多数板卡默认就是这个配置，出错也无妨 */

	uart_putsln(uart, "\r\n=== RS232 loopback test on USART6 (115200-8-N-1) ===");
	uart_putsln(uart, "Type anything in your PC serial terminal, it will echo back.");
	uart_putsln(uart, "Every 1s the MCU sends a PING line.");

	/* PING 定时 */
	int64_t last_ping = k_uptime_get();
	uint32_t seq = 0;

	/* 主循环：轮询收 + 周期发 */
	while (1) {
		/* 1) 处理接收：将所有可读字节回显（非阻塞） */
		unsigned char ch;
		while (uart_poll_in(uart, &ch) == 0) {
			/* 可选：把 \n 统一转 \r\n，便于在 Windows 终端显示整行 */
			if (ch == '\n') {
				uart_poll_out(uart, '\r');
			}
			uart_poll_out(uart, ch);
		}

		/* 2) 每 1s 主动发一行 PING */
		int64_t now = k_uptime_get();
		if (now - last_ping >= 1000) {
			last_ping = now;
			char line[48];
			int n = snprintf(line, sizeof(line), "PING %lu", (unsigned long)seq++);
			if (n > 0) {
				uart_putsln(uart, line);
			}
		}

		/* 稍微让出 CPU，避免纯忙等 */
		k_msleep(1);
	}
}
