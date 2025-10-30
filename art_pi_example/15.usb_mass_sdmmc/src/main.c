#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <ff.h>



int main(void)
{
   int rc;

    rc = disk_access_init("SD");
    printk("disk_access_init rc=%d\n", rc);

#ifdef CONFIG_FILE_SYSTEM_MKFS
    // /* 一次性在裸盘上创建 FAT（不挂载，直接指定设备名） */
    // rc = fs_mkfs(FS_FATFS, (uintptr_t)"SD", NULL, 0);
    // printk("fs_mkfs(FAT,RAM) rc=%d\n", rc);
#endif

    rc = usb_enable(NULL);
    printk("usb_enable rc=%d\n", rc);

	while (1) {
		k_sleep(K_MSEC(1000));
	}
	return 0;
}

