#include "controls.h"

#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define BUTTON_DEBOUNCE_MS 30

static const struct gpio_dt_spec button =
    GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec record_led =
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static struct gpio_callback button_cb;
static struct k_work_delayable button_debounce_work;
static controls_button_handler_t button_pressed_handler;

static void button_debounce_handler(struct k_work *work)
{
    static int last_pressed = 0;

    /* 使用逻辑状态，GPIO 的有效电平由设备树处理。 */
    int pressed = gpio_pin_get_dt(&button);
    if (pressed < 0)
    {
        printk("Button read failed : %d\n", pressed);
        return;
    }

    if (pressed == last_pressed)
    {
        return;
    }
    last_pressed = pressed;

    /* 松开只更新状态；下一次按下才再次通知应用层。 */
    if (pressed == 0)
    {
        return;
    }

    button_pressed_handler();
}

static void button_irq_handler(const struct device *port,
                               struct gpio_callback *cb, uint32_t pins)
{
    /* 每次边沿都重新计时，等待抖动结束后再读取按键状态。 */
    k_work_reschedule(&button_debounce_work, K_MSEC(BUTTON_DEBOUNCE_MS));
}

int controls_init(controls_button_handler_t on_pressed)
{
    if (on_pressed == NULL)
    {
        return -EINVAL;
    }

    if (!gpio_is_ready_dt(&button) || !gpio_is_ready_dt(&record_led))
    {
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret != 0)
    {
        return ret;
    }

    ret = gpio_pin_configure_dt(&record_led, GPIO_OUTPUT_INACTIVE);
    if (ret != 0)
    {
        return ret;
    }

    /* 应用回调、消抖工作和 GPIO 回调都就绪后，才允许按键中断。 */
    button_pressed_handler = on_pressed;
    k_work_init_delayable(&button_debounce_work, button_debounce_handler);
    gpio_init_callback(&button_cb, button_irq_handler, BIT(button.pin));

    ret = gpio_add_callback(button.port, &button_cb);
    if (ret != 0)
    {
        return ret;
    }

    return gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
}

int controls_set_recording_led(bool recording)
{
    return gpio_pin_set_dt(&record_led, recording);
}
