## zephyr查找方法作参考

zephyr,sdmmc-disk  这个是将sd卡用作文件系统，我想要知道别人的设备树是怎么写的

搜索、zephyr,sdmmc-disk 

```
cd ~/zephyrproject/zephyr

grep -n "zephyr,sdmmc-disk"

限制搜索目录 
# 递归搜 boards/ 和 samples/ 两个目录
grep -Rn "zephyr,sdmmc-disk" boards/ samples/
# 或者（Bash/Zsh 花括号展开）
grep -Rn "zephyr,sdmmc-disk" {boards,samples}/
```

可以在下面的文件中查看相关的示例

```
hdj@hdj-virtual-machine:~/zephyrproject/zephyr$ grep -Rn "zephyr,sdmmc-disk" boards/ samples/
boards/arduino/mkrzero/arduino_mkrzero.dts:92:                  compatible = "zephyr,sdmmc-disk";
boards/shields/adafruit_data_logger/adafruit_data_logger.overlay:48:                    compatible = "zephyr,sdmmc-disk";
boards/shields/seeed_xiao_round_display/seeed_xiao_round_display.overlay:92:                    compatible = "zephyr,sdmmc-disk";
boards/shields/waveshare_epaper/dts/waveshare_epaper_common.dtsi:21:                    compatible = "zephyr,sdmmc-disk";
boards/shields/adafruit_2_8_tft_touch_v2/dts/adafruit_2_8_tft_touch_v2.dtsi:62:                 compatible = "zephyr,sdmmc-disk";
boards/shields/sparkfun_carrier_asset_tracker/sparkfun_carrier_asset_tracker.overlay:38:                        compatible = "zephyr,sdmmc-disk";
boards/shields/seeed_xiao_expansion_board/seeed_xiao_expansion_board.overlay:64:                        compatible = "zephyr,sdmmc-disk";
boards/shields/v2c_daplink/v2c_daplink.overlay:44:                      compatible = "zephyr,sdmmc-disk";
boards/shields/v2c_daplink/v2c_daplink_cfg.overlay:36:                  compatible = "zephyr,sdmmc-disk";
boards/shields/pmod_sd/pmod_sd.overlay:18:              compatible = "zephyr,sdmmc-disk";
boards/shields/m5stack_cardputer/m5stack_cardputer.overlay:128:                 compatible = "zephyr,sdmmc-disk";
boards/shields/adafruit_adalogger_featherwing/adafruit_adalogger_featherwing.overlay:25:                        compatible = "zephyr,sdmmc-disk";
boards/sipeed/longan_nano/longan_nano-common.dtsi:178:                  compatible = "zephyr,sdmmc-disk";
boards/ezurio/bl5340_dvk/bl5340_dvk_nrf5340_cpuapp_common.dtsi:264:                     compatible = "zephyr,sdmmc-disk";
boards/ezurio/mg100/mg100.dts:164:                      compatible = "zephyr,sdmmc-disk";
boards/espressif/esp_wrover_kit/esp_wrover_kit_procpu.dts:217:                  compatible = "zephyr,sdmmc-disk";
boards/espressif/esp32s3_eye/esp32s3_eye_procpu.dts:229:                        compatible = "zephyr,sdmmc-disk";
boards/renesas/rcar_h3ulcb/rcar_h3ulcb_r8a77951_a57.dts:69:             compatible = "zephyr,sdmmc-disk";
boards/renesas/mck_ra8t1/mck_ra8t1.dts:245:             compatible = "zephyr,sdmmc-disk";
boards/seeed/xiao_esp32s3/xiao_esp32s3_procpu_sense.dts:70:                     compatible = "zephyr,sdmmc-disk";
boards/seeed/wio_terminal/wio_terminal.dts:290:                 compatible = "zephyr,sdmmc-disk";
boards/lilygo/tdongle_s3/tdongle_s3_procpu.dts:155:                     compatible = "zephyr,sdmmc-disk";
boards/lilygo/ttgo_t8s3/ttgo_t8s3_procpu.dts:99:                        compatible = "zephyr,sdmmc-disk";
boards/lilygo/ttgo_lora32/ttgo_lora32_esp32_procpu.dts:147:                     compatible = "zephyr,sdmmc-disk";
boards/atmel/sam/sam4e_xpro/sam4e_xpro.dts:236:         compatible = "zephyr,sdmmc-disk";
boards/atmel/sam/sam_e70_xplained/sam_e70_xplained-common.dtsi:252:             compatible = "zephyr,sdmmc-disk";
boards/atmel/sam/sam_v71_xult/sam_v71_xult-common.dtsi:369:             compatible = "zephyr,sdmmc-disk";
boards/microchip/sam/sama7g54_ek/sama7g54_ek.dts:215:           compatible = "zephyr,sdmmc-disk";
boards/ruiside/ra8d1_vision_board/ra8d1_vision_board.dts:211:           compatible = "zephyr,sdmmc-disk";
boards/olimex/olimexino_stm32/olimexino_stm32.dts:144:                  compatible = "zephyr,sdmmc-disk";
boards/nxp/mimxrt1060_evk/mimxrt1060_evk.dtsi:261:              compatible = "zephyr,sdmmc-disk";
boards/nxp/frdm_k64f/frdm_k64f.dts:172:                 compatible = "zephyr,sdmmc-disk";
boards/nxp/mimxrt1024_evk/mimxrt1024_evk.dts:254:               compatible = "zephyr,sdmmc-disk";
boards/nxp/mimxrt1062_fmurt6/mimxrt1062_fmurt6.dts:437:         compatible = "zephyr,sdmmc-disk";
boards/nxp/lpcxpresso55s28/lpcxpresso55s28.dts:165:             compatible = "zephyr,sdmmc-disk";
boards/nxp/imx93_evk/imx93_evk_mimx9352_a55.dts:243:            compatible = "zephyr,sdmmc-disk";
boards/nxp/mimxrt1020_evk/mimxrt1020_evk.dts:219:               compatible = "zephyr,sdmmc-disk";
boards/nxp/lpcxpresso55s69/lpcxpresso55s69_lpc55s69_cpu0.dts:129:               compatible = "zephyr,sdmmc-disk";
boards/nxp/mimxrt1064_evk/mimxrt1064_evk.dts:289:               compatible = "zephyr,sdmmc-disk";
boards/nxp/vmu_rt1170/vmu_rt1170_mimxrt1176_cm7.dts:479:                compatible = "zephyr,sdmmc-disk";
boards/nxp/mimxrt1050_evk/mimxrt1050_evk.dtsi:253:              compatible = "zephyr,sdmmc-disk";
boards/nxp/mimxrt1180_evk/mimxrt1180_evk.dtsi:292:              compatible = "zephyr,sdmmc-disk";
boards/nxp/mimxrt685_evk/mimxrt685_evk_mimxrt685s_cm33.dts:360:         compatible = "zephyr,sdmmc-disk";
boards/nxp/mimxrt1170_evk/mimxrt1170_evk_mimxrt1176_cm7.dts:132:                compatible = "zephyr,sdmmc-disk";
boards/nxp/frdm_mcxn947/frdm_mcxn947_mcxn947_cpu0.dtsi:190:             compatible = "zephyr,sdmmc-disk";
boards/nxp/mcx_n9xx_evk/mcx_n9xx_evk_mcxn947_cpu0.dtsi:190:             compatible = "zephyr,sdmmc-disk";
boards/pjrc/teensy4/teensy41.dts:77:            compatible = "zephyr,sdmmc-disk";
boards/adafruit/feather_adalogger_rp2040/adafruit_feather_adalogger_rp2040.dts:130:                     compatible = "zephyr,sdmmc-disk";
boards/adafruit/grand_central_m4_express/adafruit_grand_central_m4_express.dts:76:                      compatible = "zephyr,sdmmc-disk";
boards/adafruit/metro_rp2040/adafruit_metro_rp2040.dts:119:                     compatible = "zephyr,sdmmc-disk";
boards/madmachine/mm_feather/mm_feather.dts:179:                compatible = "zephyr,sdmmc-disk";
boards/madmachine/mm_swiftio/mm_swiftio.dts:193:                compatible = "zephyr,sdmmc-disk";
boards/intel/socfpga/agilex5_socdk/intel_socfpga_agilex5_socdk.dts:29:          compatible = "zephyr,sdmmc-disk";
boards/hardkernel/odroid_go/odroid_go_procpu.dts:154:                   compatible = "zephyr,sdmmc-disk";
boards/m5stack/m5stack_cores3/m5stack_cores3_procpu_common.dtsi:229:                    compatible = "zephyr,sdmmc-disk";
boards/m5stack/m5stack_core2/m5stack_core2_procpu.dts:225:                      compatible = "zephyr,sdmmc-disk";
boards/m5stack/m5stack_fire/m5stack_fire_procpu.dts:171:                        compatible = "zephyr,sdmmc-disk";
boards/nordic/nrf5340_audio_dk/nrf5340_audio_dk_nrf5340_cpuapp_common.dtsi:227:                 compatible = "zephyr,sdmmc-disk";
samples/subsys/usb/mass/sample.yaml:76:    filter: dt_compat_enabled("zephyr,sdmmc-disk")
samples/subsys/fs/fs_sample/boards/nrf52840_blip.overlay:16:                    compatible = "zephyr,sdmmc-disk";
samples/subsys/fs/fs_sample/boards/nucleo_f429zi.overlay:13:                    compatible = "zephyr,sdmmc-disk";
samples/subsys/fs/fs_sample/boards/hifive_unmatched.overlay:15:                 compatible = "zephyr,sdmmc-disk";
samples/subsys/fs/fs_sample/sample.yaml:72:    filter: dt_compat_enabled("zephyr,sdmmc-disk")
samples/subsys/fs/fs_sample/README.rst:22:to run with any other board that has "zephyr,sdmmc-disk" DT node enabled.
samples/subsys/fs/littlefs/README.rst:94:It is possible that both the ``zephyr,sdmmc-disk`` and ``zephyr,mmc-disk`` block devices will be

```



