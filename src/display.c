
#include "display.h"

#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>
#include <zephyr/sys/printk.h>

/* 从设备树中获取 OLED 设备 */
static const struct device *oled_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

int oled_init(void)
{
    int ret;

    /* 1. 检查设备是否初始化成功 */
    if (!device_is_ready(oled_dev)) {
        printk("OLED device not ready\n");
        return -ENODEV;
    }

    /* 2. 设置单色像素格式 */
    ret = display_set_pixel_format(oled_dev, PIXEL_FORMAT_MONO10);

    if (ret != 0) {
        ret = display_set_pixel_format(oled_dev, PIXEL_FORMAT_MONO01);

        if (ret != 0) {
            printk("OLED pixel format error\n");
            return ret;
        }
    }

    /* 3. 初始化字符帧缓冲 */
    ret = cfb_framebuffer_init(oled_dev);
    if (ret != 0) {
        printk("CFB init failed: %d\n", ret);
        return ret;
    }

    /* 4. 清除帧缓冲 */
    ret = cfb_framebuffer_clear(oled_dev, true);
    if (ret != 0) {
        return ret;
    }

    /* 5. 开启显示 */
    ret = display_blanking_off(oled_dev);
    if (ret != 0 && ret != -ENOSYS) {
        return ret;
    }

    /* 6. 在帧缓冲中绘制文字 */
    ret = cfb_print(oled_dev, "Hello Zephyr!", 0, 0);
    if (ret != 0) {
        return ret;
    }

    ret = cfb_print(oled_dev, "OLED READY", 0, 16);
    if (ret != 0) {
        return ret;
    }

    /* 7. 将帧缓冲提交到屏幕 */
    ret = cfb_framebuffer_finalize(oled_dev);
    if (ret != 0) {
        return ret;
    }

    printk("OLED initialized successfully\n");

    return 0;
}
