
#include "display.h"

#include <errno.h>
#include <stdbool.h>

#include <sys/errno.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>
#include <zephyr/sys/printk.h>

static const struct device *oled_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static bool oled_ready = false;

static int select_small_font(void)
{
    int count = cfb_get_numof_fonts(oled_dev);

    for (int i = 0; i < count; i++) {
        uint8_t width = 0;
        uint8_t height = 0;

        int ret = cfb_get_font_size(oled_dev, i, &width, &height);

        if (ret != 0) {
            return ret;
        }

        if (width == 5 && height == 8) {
            ret = cfb_framebuffer_set_font(oled_dev, i);
            /* 字符之间增加 1 像素间距 */
            return cfb_set_kerning(oled_dev, 1);
        }
    }
    return -ENOENT;
}


int app_display_init(void)
{
    int ret;
    if (oled_ready) {
        return 0;
    }
    if (!device_is_ready(oled_dev)) {
        return -ENODEV;
    }
    /*设置单色像素格式*/
    ret = display_set_pixel_format(oled_dev, PIXEL_FORMAT_MONO01);
    /*初始化字符帧缓冲*/
    ret = cfb_framebuffer_init(oled_dev);
    /*选择字体*/
    ret = select_small_font();

    /*清空缓冲区和屏幕*/
    ret = cfb_framebuffer_clear(oled_dev, true);
    /*打开显示*/
    display_blanking_off(oled_dev);

    oled_ready = true;
    return 0;
}

int app_display_draw_layout(display_mode_t mode)
{
    int ret;
    if (!oled_ready) {
        return -ENODEV;
    }
    if (mode != DISPLAY_MODE_16 && mode != DISPLAY_MODE_32) {
        return -EINVAL;
    }

    ret = cfb_framebuffer_clear(oled_dev, false);
    /*顶部标题*/
    const char *mode_text = (mode == DISPLAY_MODE_16) ? "16CH" : "32CH";
    ret = cfb_print(oled_dev, mode_text, 0, 0);
    cfb_print(oled_dev, "FREQ:", 30, 0);
    /*标题分割线*/
    struct cfb_position top_start = {0, 9};
    struct cfb_position top_end = {127, 9};
    ret = cfb_draw_line(oled_dev, &top_start, &top_end);
    /*频谱底部边界*/
    struct cfb_position bottom_start = {0, 63};
    struct cfb_position bottom_end  = {127, 63};
    ret = cfb_draw_line(oled_dev, &bottom_start, &bottom_end);

    /*一次性提交画面*/
    return cfb_framebuffer_finalize(oled_dev);
}
