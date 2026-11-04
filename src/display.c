
#include "display.h"

#include <errno.h>
#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>
#include <zephyr/sys/printk.h>

static const struct device *oled_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static bool oled_ready;

int oled_init(void)
{
    int ret;

    if (oled_ready) {
        return 0;
    }

    if (!device_is_ready(oled_dev)) {
        return -ENODEV;
    }

    ret = display_set_pixel_format(oled_dev, PIXEL_FORMAT_MONO10);

    if (ret != 0) {
        ret = display_set_pixel_format(oled_dev, PIXEL_FORMAT_MONO01);

        if (ret != 0) {
            return ret;
        }
    }

    ret = cfb_framebuffer_init(oled_dev);
    if (ret != 0) {
        return ret;
    }

    ret = cfb_framebuffer_clear(oled_dev, true);
    if (ret != 0) {
        cfb_framebuffer_deinit(oled_dev);
        return ret;
    }

    ret = display_blanking_off(oled_dev);
    if (ret != 0 && ret != -ENOSYS) {
        cfb_framebuffer_deinit(oled_dev);
        return ret;
    }

    oled_ready = true;

    printk("OLED initialized\n");
    return 0;
}

int oled_show_text(const char *line1, const char *line2)
{
    int ret;

    if (!oled_ready) {
        return -ENODEV;
    }

    if (line1 == NULL || line2 == NULL) {
        return -EINVAL;
    }

    /* 清除帧缓冲，不立即刷新屏幕 */
    ret = cfb_framebuffer_clear(oled_dev, false);
    if (ret != 0) {
        return ret;
    }

    /* 第一行 */
    ret = cfb_print(oled_dev, line1, 0, 0);
    if (ret != 0) {
        return ret;
    }

    /* 第二行 */
    ret = cfb_print(oled_dev, line2, 0, 16);
    if (ret != 0) {
        return ret;
    }

    /* 将完整画面提交到 OLED */
    return cfb_framebuffer_finalize(oled_dev);
}
