
/*
  该示例使用 RAM 磁盘作为 USB 设备的存储介质。

  设备端先 mkfs FAT，再让主机使用  (不创建 FAT，第一次插入时需要手动格式化)

  CONFIG_FILE_SYSTEM_MKFS  该宏开关受 prj_conf控制,因此无需在main.c 中 #define CONFIG_FILE_SYSTEM_MKFS
*/



#include <zephyr/kernel.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/fs/fs.h>
#include <ff.h>
#include <stdio.h>
#include <stdint.h>

void main(void)
{
    int rc;

    rc = disk_access_init("RAM");
    printk("disk_access_init rc=%d\n", rc);

#ifdef CONFIG_FILE_SYSTEM_MKFS
    /* 一次性在裸盘上创建 FAT（不挂载，直接指定设备名） */
    rc = fs_mkfs(FS_FATFS, (uintptr_t)"RAM", NULL, 0);
    printk("fs_mkfs(FAT,RAM) rc=%d\n", rc);
#endif

    rc = usb_enable(NULL);
    printk("usb_enable rc=%d\n", rc);

    while (1) {
        k_sleep(K_SECONDS(1));
    }
}
