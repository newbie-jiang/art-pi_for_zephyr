## zephyr增加自己的驱动



https://blog.csdn.net/huayidw/article/details/150533682?spm=1001.2014.3001.5502

- 增加驱动文件 zephyr\drivers\input\input_gt1151.c
- 增加配置文件 zephyr\drivers\input\Kconfig.gt1151
- 增加驱动包含  zephyr\drivers\input\CMakeLists.txt  添加  

```
zephyr_library_sources_ifdef(CONFIG_INPUT_GT1151 input_gt1151.c)
```

- 增加驱动配置 zephyr\drivers\input\Kconfig 

```
source "drivers/input/Kconfig.gt1151"
```

- 增加设备树绑定 zephyr\dts\bindings\input\goodix,gt1151.yaml   

```
# Copyright (c) 2025
# SPDX-License-Identifier: Apache-2.0

description: GT1151 capacitive touch panel

compatible: "goodix,gt1151"

include: i2c-device.yaml

properties:
  irq-gpios:
    type: phandle-array
  reset-gpios:
    type: phandle-array
  alt-addr:
    type: int
    description:
      Alternate I2C address for this device. When provided, the driver will
      use probing mode to determine the I2C address rather than setting the
      INT pin low to force a specific address


```

上述五个文件为驱动文件适配，这是增加一个驱动的最少适配文件