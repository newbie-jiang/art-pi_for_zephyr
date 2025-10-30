// #pragma once

/* 先包含 HAL ETH 头，再覆盖它里面的默认宏（通常=4/1524） */
// #include <stm32h7xx_hal_eth.h>
// #include <stm32h7xx_hal_conf.h>

/* 安全起见先 undef，避免重定义告警 */
// #ifdef ETH_TX_DESC_CNT
// #undef ETH_TX_DESC_CNT
// #endif
// #ifdef ETH_RX_DESC_CNT
// #undef ETH_RX_DESC_CNT
// #endif
// #ifdef ETH_TX_BUF_SIZE
// #undef ETH_TX_BUF_SIZE
// #endif
// #ifdef ETH_RX_BUF_SIZE
// #undef ETH_RX_BUF_SIZE
// #endif

// /* 你的目标配置（只增 RAM，不涨 ROM） */
// #define ETH_TX_DESC_CNT 16
// #define ETH_RX_DESC_CNT 16
// #define ETH_TX_BUF_SIZE 1536
// #define ETH_RX_BUF_SIZE 1536
