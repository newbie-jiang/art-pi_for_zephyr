/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * STM32 + LAN8720A (RMII) 以太网连网（DHCP）
 * - 使用 Zephyr logging（自带时间戳）
 * - 接口 UP 后立即 net_dhcpv4_restart()（跳过初始随机等待）
 * - 监听链路与 IPv4 地址事件；断线->stop，重连->restart，并在拿到地址时打印
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/ethernet_mgmt.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <stm32h7xx_hal_eth.h>   // 提供 ETH_TX_DESC_CNT / ETH_RX_DESC_CNT

// LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#if !IS_ENABLED(CONFIG_LOG)
  #define LOG_INF(fmt, ...) printk(fmt "\n", ##__VA_ARGS__)
  #define LOG_WRN(fmt, ...) printk("[WRN] " fmt "\n", ##__VA_ARGS__)
  #define LOG_ERR(fmt, ...) printk("[ERR] " fmt "\n", ##__VA_ARGS__)
  #define LOG_MODULE_REGISTER(a, b)
#endif


/* 兼容：若未定义以太网 CARRIER 事件，则退回用 IF_UP/IF_DOWN */
#ifndef NET_EVENT_ETHERNET_CARRIER_ON
#define NET_EVENT_ETHERNET_CARRIER_ON  NET_EVENT_IF_UP
#endif
#ifndef NET_EVENT_ETHERNET_CARRIER_OFF
#define NET_EVENT_ETHERNET_CARRIER_OFF NET_EVENT_IF_DOWN
#endif

#define ETH_ALIAS_NODE DT_ALIAS(eth)        /* &mac */
#define ETH_RST_ALIAS  DT_ALIAS(ethphyrst)  /* 复位脚别名（可选） */

#if !DT_NODE_HAS_STATUS(ETH_ALIAS_NODE, okay)
#error "eth alias not found/disabled"
#endif

#if DT_NODE_HAS_STATUS(ETH_RST_ALIAS, okay)
static const struct gpio_dt_spec phy_rst = GPIO_DT_SPEC_GET(ETH_RST_ALIAS, gpios);
#endif

static struct net_mgmt_event_callback cb_link;
static struct net_mgmt_event_callback cb_ipv4;

static void print_ipv4_info(struct net_if *iface)
{
	char buf[NET_IPV4_ADDR_LEN];

	const struct in_addr *addr =
		net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
	if (!addr) {
		LOG_INF("IPv4 not configured");
		return;
	}

	struct in_addr mask = net_if_ipv4_get_netmask_by_addr(iface, addr);
	struct in_addr gw   = net_if_ipv4_get_gw(iface);

	LOG_INF("IPv4 addr: %s",  net_addr_ntop(AF_INET, addr,  buf, sizeof buf));
	LOG_INF("IPv4 mask: %s",  net_addr_ntop(AF_INET, &mask, buf, sizeof buf));
	LOG_INF("IPv4  gw : %s",  net_addr_ntop(AF_INET, &gw,   buf, sizeof buf));
}

static void print_mac(struct net_if *iface)
{
	const struct net_linkaddr *ll = net_if_get_link_addr(iface);
	LOG_INF("IF MAC: %02X:%02X:%02X:%02X:%02X:%02X",
		ll->addr[0], ll->addr[1], ll->addr[2],
		ll->addr[3], ll->addr[4], ll->addr[5]);
}

/* 可选：PHY 复位（低有效示例） */
static void phy_reset_pulse(void)
{
#if DT_NODE_HAS_STATUS(ETH_RST_ALIAS, okay)
	if (!device_is_ready(phy_rst.port)) {
		LOG_WRN("PHY reset GPIO not ready");
		return;
	}
	gpio_pin_configure_dt(&phy_rst, GPIO_OUTPUT_ACTIVE); /* ACTIVE=有效电平（若 ACTIVE_LOW 即拉低） */
	k_msleep(50);                                        /* 复位保持 ≥50ms 更稳 */
	gpio_pin_set_dt(&phy_rst, 0);                        /* 释放复位 */
	k_msleep(100);                                       /* 等 REF_CLK/自协商启动 */
#endif
}

/* 链路事件：掉线->stop；上线->restart（快速获取） */
static void link_event_handler(struct net_mgmt_event_callback *cb,
                               uint64_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event == NET_EVENT_ETHERNET_CARRIER_OFF) {
#if IS_ENABLED(CONFIG_NET_DHCPV4)
		(void)net_dhcpv4_stop(iface);
#endif
		LOG_INF("Link DOWN");
	} else if (mgmt_event == NET_EVENT_ETHERNET_CARRIER_ON) {
#if IS_ENABLED(CONFIG_NET_DHCPV4)
		(void)net_dhcpv4_restart(iface);
#endif
		LOG_INF("Link UP -> restart DHCP");
	}
}

/* IPv4 地址事件：新增/更新时打印（断开时提示） */
static void ipv4_event_handler(struct net_mgmt_event_callback *cb,
                               uint64_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
		LOG_INF("IPv4 acquired");
		print_ipv4_info(iface);
	} else if (mgmt_event == NET_EVENT_IPV4_ADDR_DEL) {
		LOG_INF("IPv4 address removed");
	}
}

void main(void)
{
	LOG_INF("=== STM32 + LAN8720A (RMII) DHCP ===");



	 printk("ETH_TX_DESC_CNT=%d, ETH_RX_DESC_CNT=%d\n",
           (int)ETH_TX_DESC_CNT, (int)ETH_RX_DESC_CNT);

	phy_reset_pulse();

	struct net_if *iface = net_if_get_default();
	if (!iface) {
		LOG_ERR("No default net_if");
		return;
	}

	print_mac(iface);

	/* 注册事件回调（用一个回调掩码同时订阅 ON/OFF；另一个订阅 IPv4 地址增删） */
	net_mgmt_init_event_callback(&cb_link, link_event_handler,
		NET_EVENT_ETHERNET_CARRIER_ON | NET_EVENT_ETHERNET_CARRIER_OFF |
		NET_EVENT_IF_UP | NET_EVENT_IF_DOWN);
	net_mgmt_add_event_callback(&cb_link);

	net_mgmt_init_event_callback(&cb_ipv4, ipv4_event_handler,
		NET_EVENT_IPV4_ADDR_ADD | NET_EVENT_IPV4_ADDR_DEL);
	net_mgmt_add_event_callback(&cb_ipv4);

	/* 接口 UP 后直接 restart DHCP（跳过首次随机等待） */
	net_if_up(iface);
#if IS_ENABLED(CONFIG_NET_DHCPV4)
	(void)net_dhcpv4_restart(iface);
#endif

	/* 主循环保持存活；事件回调会在每次重连时重新打印 IP */
	while (1) {
		k_sleep(K_SECONDS(5));
	}
}


