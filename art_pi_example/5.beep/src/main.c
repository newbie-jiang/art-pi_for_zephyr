/*
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

/* ====== 自定义：响/停的时长（毫秒）====== */
#define BUZZER_ON_MS   200   /* 每次响的时间 */
#define BUZZER_OFF_MS  2000   /* 每次静默的时间 */
#define BOOT_BEEP_ON   150   /* 上电提示音：单次响时长 */
#define BOOT_BEEP_OFF  150   /* 上电提示音：单次停时长 */
#define BOOT_BEEP_N    3     /* 上电提示音：次数 */
/* ====================================== */

/* 通过别名拿到蜂鸣器 GPIO 描述（设备树里 alias: buzzer0） */
#define BUZZER_NODE DT_ALIAS(buzzer0)
#if !DT_NODE_HAS_STATUS(BUZZER_NODE, okay)
#error "buzzer0 alias not found in devicetree"
#endif
static const struct gpio_dt_spec buzzer = GPIO_DT_SPEC_GET(BUZZER_NODE, gpios);

/* 一次性按照 on/off 的节奏响 cycles 次 */
static void beep_ms(int on_ms, int off_ms, int cycles)
{
	for (int i = 0; i < cycles; i++) {
		gpio_pin_set_dt(&buzzer, 1);   /* 拉高 → 响 */
		k_msleep(on_ms);
		gpio_pin_set_dt(&buzzer, 0);   /* 拉低 → 静音 */
		k_msleep(off_ms);
	}
}

int main(void)
{
	if (!device_is_ready(buzzer.port)) {
		return 0;
	}

	/* 配置为输出，初始不响 */
	if (gpio_pin_configure_dt(&buzzer, GPIO_OUTPUT_INACTIVE) < 0) {
		return 0;
	}

	/* 上电提示：嘀嘀嘀 */
	beep_ms(BOOT_BEEP_ON, BOOT_BEEP_OFF, BOOT_BEEP_N);

	/* 周期性按 BUZZER_ON_MS/BUZZER_OFF_MS 响/停 */
	while (1) {
		gpio_pin_set_dt(&buzzer, 0);
		k_msleep(BUZZER_OFF_MS);

		gpio_pin_set_dt(&buzzer, 1);
		k_msleep(BUZZER_ON_MS);
	}
}
