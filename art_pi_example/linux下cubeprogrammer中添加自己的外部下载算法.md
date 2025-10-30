# linux下cubeprogrammer中添加外部下载算法

**查找stm32cubeProgrammer安装路径**

```
which STM32_Programmer_CLI
```

```
hdj@hdj-virtual-machine:~$ which STM32_Programmer_CLI
/usr/local/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI
```

将下载算法ART-Pi_W25Q64.stldr 放到此目录下https://github.com/RT-Thread-Studio/sdk-bsp-stm32h750-realthread-artpi/tree/master/debug/stldr



```
/usr/local/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/ExternalLoader
```

![image-20250914134852018](https://newbie-typora.oss-cn-shenzhen.aliyuncs.com/TyporaJPG/image-20250914134852018.png)

## 下载时指定外部下载算法

下载时指定下载算法  --extload "/usr/local/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/ExternalLoader/ART-Pi_W25Q64.stldr"

```
west flash -d build/with_mcuboot -r stm32cubeprogrammer --skip-rebuild \
  --extload "/usr/local/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/ExternalLoader/ART-Pi_W25Q64.stldr"
```



## 每次下载时不指定外部下载算法，可以跟随boards的cmake



添加这一行到zephyr\boards\ruiside\art_pi\board.cmake

```
board_runner_args(stm32cubeprogrammer "--extload=ART-Pi_W25Q64.stldr")
```



```
# SPDX-License-Identifier: Apache-2.0

# keep first
board_runner_args(stm32cubeprogrammer "--port=swd" "--reset-mode=hw")
board_runner_args(stm32cubeprogrammer "--extload=ART-Pi_W25Q64.stldr")

board_runner_args(openocd --target-handle=_CHIPNAME.cpu0)

# keep first
include(${ZEPHYR_BASE}/boards/common/stm32cubeprogrammer.board.cmake)
```

