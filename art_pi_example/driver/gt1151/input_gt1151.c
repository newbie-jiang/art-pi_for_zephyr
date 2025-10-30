

/* drivers/input/input_gt1151.c */
#define DT_DRV_COMPAT goodix_gt1151

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gt1151, CONFIG_INPUT_LOG_LEVEL);

/* ===== Registers ===== */
#define REG_PRODUCT_ID   0x8140U   /* char[4], e.g. "1151"/"1158" */
#define REG_STATUS       0x814EU   /* bit7: ready, bit3..0: num points */
#define REG_REQUEST      0x8044U   /* clear to ACK firmware request */
#define REG_POINT_0      0x814FU   /* points start, 8 bytes/point */

/* REG_STATUS bits */
#define TOUCH_POINTS_MSK 0x0FU
#define TOUCH_STATUS_MSK (1U << 7)

#define GT_POINT_SIZE    8

/* ===== Devicetree config / runtime data ===== */
struct gt1151_config {
	struct i2c_dt_spec bus;
	struct gpio_dt_spec rst_gpio;  /* optional */
	struct gpio_dt_spec int_gpio;
	uint8_t alt_addr;              /* optional alternate I2C address */
};

struct gt1151_data {
	const struct device *dev;
	struct k_work work;
	uint8_t actual_address;
#ifdef CONFIG_INPUT_GT1151_INTERRUPT
	struct gpio_callback int_gpio_cb;
#else
	struct k_timer timer;
#endif
};

/* ===== I2C helpers (BE16 register, robust transfer) ===== */
static int reg_read(const struct device *dev, uint16_t reg, void *buf, size_t len)
{
	const struct gt1151_config *cfg = dev->config;
	struct gt1151_data *data = dev->data;

	uint8_t ra[2];
	sys_put_be16(reg, ra);

	struct i2c_msg msgs[2] = {
		{ .buf = ra,  .len = 2,   .flags = I2C_MSG_WRITE },
		{ .buf = buf, .len = len, .flags = I2C_MSG_READ | I2C_MSG_RESTART | I2C_MSG_STOP },
	};

	int r = i2c_transfer(cfg->bus.bus, msgs, 2, data->actual_address);
	if (r) {
		/* Fallback: some controllers/devices prefer write_read */
		r = i2c_write_read(cfg->bus.bus, data->actual_address, ra, 2, buf, len);
	}
	return r;
}

static int reg_write_u8(const struct device *dev, uint16_t reg, uint8_t val)
{
	const struct gt1151_config *cfg = dev->config;
	struct gt1151_data *data = dev->data;

	uint8_t frame[3];
	sys_put_be16(reg, frame);
	frame[2] = val;
	return i2c_write(cfg->bus.bus, frame, sizeof(frame), data->actual_address);
}

/* ===== 读取并上报一“帧”；返回值：>0 处理了帧，=0 无新帧，<0 出错 =====
 * 点格式(每点 8B):
 *   [0]=ID低4位, [1]=X_L, [2]=X_H, [3]=Y_L, [4]=Y_H, [5]=Size_L, [6]=Size_H, [7]=EVT/保留
 * 流程：读 status -> 读 N*8 -> 清 status -> 上报 -> 统一 submit 一次
 */
static int gt1151_process_frame(const struct device *dev)
{
	int r;
	uint8_t status = 0;

	r = reg_read(dev, REG_STATUS, &status, 1);
	if (r < 0) {
		return r;
	}
	if ((status & TOUCH_STATUS_MSK) == 0) {
		return 0; /* no new frame */
	}

	uint8_t points = status & TOUCH_POINTS_MSK;
	if (points > CONFIG_INPUT_GT1151_MAX_TOUCH_POINTS) {
		points = CONFIG_INPUT_GT1151_MAX_TOUCH_POINTS;
	}

	uint8_t buf[CONFIG_INPUT_GT1151_MAX_TOUCH_POINTS * GT_POINT_SIZE] = {0};
	size_t need = points * GT_POINT_SIZE;

	if (points) {
		r = reg_read(dev, REG_POINT_0, buf, need);
		if (r < 0) {
			return r;
		}
	}

	/* Clear status AFTER reading the frame (latch next frame) */
	(void)reg_write_u8(dev, REG_STATUS, 0x00);

	/* 报告所有点（不 submit，不发 BTN_TOUCH）；最后统一 SYN 一次 */
	for (uint8_t i = 0; i < points; i++) {
		const uint8_t *p = &buf[i * GT_POINT_SIZE];

		uint8_t  id = (p[0] & 0x0F);                     /* Track ID 低4位 */
		uint16_t x  = ((uint16_t)p[2] << 8) | p[1];       /* X_H:X_L */
		uint16_t y  = ((uint16_t)p[4] << 8) | p[3];       /* Y_H:Y_L */

		/* 限制在编译上限范围，避免 slot 越界 */
		id %= CONFIG_INPUT_GT1151_MAX_TOUCH_POINTS;

#if CONFIG_INPUT_GT1151_MAX_TOUCH_POINTS > 1
		input_report_abs(dev, INPUT_ABS_MT_SLOT, id, false, K_NO_WAIT);
#endif
		input_report_abs(dev, INPUT_ABS_X, x, false, K_NO_WAIT);
		input_report_abs(dev, INPUT_ABS_Y, y, false, K_NO_WAIT);
	}

	/* 统一提交一次：有点→BTN_TOUCH=1；无点→BTN_TOUCH=0（这帧不会走到这里的无点分支） */
	input_report_key(dev, INPUT_BTN_TOUCH, (points > 0) ? 1 : 0, true, K_NO_WAIT);

	return 1;
}

/* ===== Work / ISR / Timer ===== */
static void work_handler(struct k_work *work)
{
	struct gt1151_data *data = CONTAINER_OF(work, struct gt1151_data, work);
	const struct gt1151_config *cfg = data->dev->config;

#ifdef CONFIG_INPUT_GT1151_INTERRUPT
	/* 先关中断，处理完这一批帧后再开，避免中断风暴 */
	(void)gpio_pin_interrupt_configure_dt(&cfg->int_gpio, GPIO_INT_DISABLE);
#endif

	/* 把这次积压的帧都读干净 */
	while (true) {
		int rc = gt1151_process_frame(data->dev);
		if (rc <= 0) {
			break;
		}
	}

#ifdef CONFIG_INPUT_GT1151_INTERRUPT
	(void)gpio_pin_interrupt_configure_dt(&cfg->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
#endif
}

#ifdef CONFIG_INPUT_GT1151_INTERRUPT
static void isr_handler(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(pins);
	struct gt1151_data *data = CONTAINER_OF(cb, struct gt1151_data, int_gpio_cb);
	k_work_submit(&data->work);
}
#else
static void timer_handler(struct k_timer *tmr)
{
	struct gt1151_data *data = CONTAINER_OF(tmr, struct gt1151_data, timer);
	k_work_submit(&data->work);
}
#endif

/* ===== Probe helpers ===== */
static int try_probe_address(const struct device *dev, uint8_t addr)
{
	struct gt1151_data *data = dev->data;
	data->actual_address = addr;

	char id[4] = {0};
	int r = reg_read(dev, REG_PRODUCT_ID, id, sizeof(id));
	if (r < 0) {
		return r;
	}

	/* Accept "115*" (e.g., 1151 / 1158) */
	if (id[0] == '1' && id[1] == '1' && id[2] == '5') {
		LOG_INF("GT115x ID: %c%c%c%c", id[0], id[1], id[2], id[3]);
		return 0;
	}
	return -ENODEV;
}

/* ===== Init =====
 * - INT as input (pull-up via DTS)
 * - Optional reset: assert (ACTIVE) >=10ms, release, wait >=60ms
 * - Probe primary DTS address; if alt-addr provided, try it as fallback
 */
static int gt1151_init(const struct device *dev)
{
	const struct gt1151_config *cfg = dev->config;
	struct gt1151_data *data = dev->data;
	int r;

	if (!i2c_is_ready_dt(&cfg->bus)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	data->dev = dev;
	k_work_init(&data->work, work_handler);

	/* INT input (with pull-up from DTS) */
	if (!gpio_is_ready_dt(&cfg->int_gpio)) {
		LOG_ERR("INT gpio not ready");
		return -ENODEV;
	}
	r = gpio_pin_configure_dt(&cfg->int_gpio, GPIO_INPUT);
	if (r < 0) {
		LOG_ERR("INT set input failed");
		return r;
	}

	/* Optional hardware reset */
	if (cfg->rst_gpio.port) {
		if (!gpio_is_ready_dt(&cfg->rst_gpio)) {
			LOG_ERR("RST gpio not ready");
			return -ENODEV;
		}
		r = gpio_pin_configure_dt(&cfg->rst_gpio, GPIO_OUTPUT_INACTIVE); /* drive INACTIVE first */
		if (r < 0) {
			LOG_ERR("RST config failed");
			return r;
		}
		gpio_pin_set_dt(&cfg->rst_gpio, 1);  /* assert reset (ACTIVE) */
		k_sleep(K_MSEC(12));                 /* >= 10ms */
		gpio_pin_set_dt(&cfg->rst_gpio, 0);  /* release reset (INACTIVE) */
		k_sleep(K_MSEC(60));                 /* >= 60ms */
	}

#ifdef CONFIG_INPUT_GT1151_INTERRUPT
	r = gpio_pin_interrupt_configure_dt(&cfg->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (r < 0) {
		LOG_ERR("INT irq config failed");
		return r;
	}
#endif

	/* Probe primary address from DTS, then optional alt-addr */
	data->actual_address = cfg->bus.addr;   /* should be 0x14 */
	r = try_probe_address(dev, data->actual_address);
	if (r < 0 && cfg->alt_addr) {
		data->actual_address = cfg->alt_addr;
		r = try_probe_address(dev, data->actual_address);
		if (r == 0) {
			LOG_INF("Using alt I2C addr 0x%02x", data->actual_address);
		}
	}
	if (r < 0) {
		LOG_ERR("GT115x not responding at 0x%02x%s",
			cfg->bus.addr, cfg->alt_addr ? " or alt" : "");
		return r;
	}

	/* Clear possible firmware request so normal reporting can start */
	(void)reg_write_u8(dev, REG_REQUEST, 0x00);

#ifdef CONFIG_INPUT_GT1151_INTERRUPT
	static struct gt1151_data *s_data; /* one instance per instantiation */
	s_data = data;
	static struct gpio_callback s_cb;
	data->int_gpio_cb = s_cb;
	gpio_init_callback(&data->int_gpio_cb, isr_handler, BIT(cfg->int_gpio.pin));

	r = gpio_add_callback(cfg->int_gpio.port, &data->int_gpio_cb);
	if (r < 0) {
		LOG_ERR("gpio_add_callback failed");
		return r;
	}

	LOG_INF("Mode: interrupt, INT=%s/%d (edge-to-active)",
		cfg->int_gpio.port->name, cfg->int_gpio.pin);
#else
	k_timer_init(&data->timer, timer_handler, NULL);
	k_timer_start(&data->timer, K_MSEC(CONFIG_INPUT_GT1151_PERIOD_MS),
		      K_MSEC(CONFIG_INPUT_GT1151_PERIOD_MS));
	LOG_INF("Mode: polling, period=%d ms", CONFIG_INPUT_GT1151_PERIOD_MS);
#endif

	LOG_INF("GT115x ready (addr=0x%02x)", data->actual_address);
	return 0;
}

/* ===== Instance boilerplate ===== */
#define GT1151_INIT(inst)                                                        \
	static const struct gt1151_config cfg_##inst = {                         \
		.bus      = I2C_DT_SPEC_INST_GET(inst),                          \
		.rst_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, (struct gpio_dt_spec){0}), \
		.int_gpio = GPIO_DT_SPEC_INST_GET(inst, irq_gpios),              \
		.alt_addr = DT_INST_PROP_OR(inst, alt_addr, 0),                  \
	};                                                                         \
	static struct gt1151_data data_##inst;                                     \
	DEVICE_DT_INST_DEFINE(inst, gt1151_init, NULL, &data_##inst, &cfg_##inst,  \
			      POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(GT1151_INIT)

