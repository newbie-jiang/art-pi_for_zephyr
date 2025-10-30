# [ART-PI  for Zephyr](https://docs.zephyrproject.org/latest/boards/ruiside/art_pi/doc/index.html)



![art_pi](https://cloud.rocketpi.club/cloud/art_pi.webp)

# Support hardware 

## Basic development board

- [x] LED
- [x] UART
- [x] BUTTON
- [x] FMC SDRAM
- [x] LTDC 800x480
- [x] TOUCH GT1151
- [x] QSPI  W25Q64JV
- [x] SPI W25Q128JV
- [x] SDMMC SD
- [x] USB 

## Industrial expansion board

- [x] BUZZER
- [x] RS232
- [x] RS485
- [x] ETH
- [ ] FDCAN
- [ ] DC_IN

## Examples that can be used directly

Build to internal flash（128K）

```
west build -p always -b art_pi samples/basic/blinky

west build -p always -b art_pi samples/hello_world

west build -p always -b art_pi samples/basic/button

west build -p always -b art_pi samples/drivers/spi_flash

west build -p always -b art_pi samples/subsys/usb/cdc_acm

west build -p always -b art_pi samples/subsys/fs/fs_sample -- \
  -DCONFIG_SDMMC_STM32_CLOCK_CHECK=n 

west build -p always -b art_pi samples/drivers/display -- \
  -DCONFIG_MEMC=y \
  -DCONFIG_HEAP_MEM_POOL_SIZE=102400
    
west build -p always -b art_pi samples/subsys/input/draw_touch_events -- \
  -DCONFIG_I2C=y \
  -DCONFIG_INPUT_GT911=y \
  -DCONFIG_INPUT_GT911_INTERRUPT=y \
  -DCONFIG_INPUT_THREAD_STACK_SIZE=2048 \
  -DCONFIG_MEMC=y \
  -DCONFIG_HEAP_MEM_POOL_SIZE=102400

west build -p always -b art_pi samples/subsys/input/input_dump -- \
  -DCONFIG_I2C=y \
  -DCONFIG_INPUT_GT911=y \
  -DCONFIG_INPUT_GT911_INTERRUPT=y
```

## Provide additional examples

```
hdj@hdj-virtual-machine:~/zephyrproject/art_pi_example$ tree -L 1
.
├── 1.with_mcuboot
├── 2.app
├── 3.fmc_sdram_speedtest
├── 4.sdmmc_sd_speedtest
├── 5.beep
├── 6.rs232
├── 7.rs485
├── 8.eth
├── 9.eth_speed_test
├── 10.touch_gt1151
├── 11.lvgl
├── 12.lvgl_demos
├── 13.usb_mass_ram
├── 15.usb_mass_sdmmc
├── 16.webusb
├── 17.hid_keyboard
├── 18.hid_mouse
├── 19.uvc

```

## The file structure followed by the additional examples

```
hdj@hdj-virtual-machine:~/zephyrproject$ tree -L 1
.
├── art_pi_example
├── bootloader
├── modules
├── optional
├── tools
├── zephyr

art_pi_example目录下存放额外的示例

```

## download tools 

read  **linux下cubeprogrammer中添加自己的外部下载算法.md**

## mcuboot（The use of additional examples requires the inclusion of them.）

add file  **art_pi.conf**  for  **bootloader/mcuboot/boot/zephyr/boards/**

art_pi.conf

```
CONFIG_BOOT_DIRECT_XIP=y
CONFIG_STM32_MEMMAP=y
CONFIG_BOOT_MAX_IMG_SECTORS_AUTO=n
CONFIG_BOOT_MAX_IMG_SECTORS=4096
```

## serial

```
# find
ls /dev/ttyUSB* /dev/ttyACM*

# open
picocom -b 115200 /dev/ttyACM0
```

Common picocom shortcut keys

- **Ctrl + A, Ctrl + Q** →  挂起串口，不退出
- **Ctrl + A, Ctrl + Z** → 帮助菜单
- **Ctrl + A, Ctrl + X** → 退出并关闭设备

