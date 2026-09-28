#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <errno.h>
#include "audio.h"

/* 0：关闭串口音频输出；1：开启串口音频输出 */
#define AUDIO_UART_DEBUG 0

//应用层内存池
#define APP_AUDIO_BLOCK_SIZE  512
#define APP_AUDIO_BLOCK_COUNT 4
#define AUDIO_GAIN 8

//准备两个线程的栈
#define CAPTURE_STACK_SIZE 1024
#define PROCESS_STACK_SIZE 1024

#define CAPTURE_PRIORITY 5
#define PROCESS_PRIORITY 7

K_THREAD_STACK_DEFINE(capture_stack, CAPTURE_STACK_SIZE);
K_THREAD_STACK_DEFINE(process_stack, PROCESS_STACK_SIZE);

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0),gpios);
static const struct gpio_dt_spec record_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0),gpios);

static struct k_thread capture_thread;
static struct k_thread process_thread;

#if AUDIO_UART_DEBUG
static const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#endif

K_MEM_SLAB_DEFINE_STATIC(app_audio_slab, APP_AUDIO_BLOCK_SIZE, APP_AUDIO_BLOCK_COUNT,4);

struct audio_block
{
    void *buffer;
    size_t size;
};
//消息队列
K_MSGQ_DEFINE(audio_msgq, sizeof(struct audio_block), APP_AUDIO_BLOCK_COUNT, 4);

static int controls_init(void)
{
    if (!gpio_is_ready_dt(&button) || !gpio_is_ready_dt(&record_led))
    {
        return -ENODEV;
    }
    int ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
    if (ret != 0)
    {
        return ret;
    }
    return gpio_pin_configure_dt(&record_led, GPIO_OUTPUT_INACTIVE);
}

static void audio_capture_task(void *p1, void *p2, void *p3)
{
    while (1)
    {
        void *buffer;
        int ret = k_mem_slab_alloc(&app_audio_slab, &buffer, K_FOREVER);
        size_t size;
        ret = audio_read(buffer, APP_AUDIO_BLOCK_SIZE, &size);
        if (ret != 0)
        {
            k_mem_slab_free(&app_audio_slab, buffer);
            continue;
        }
        struct audio_block block =
        {
            .buffer = buffer,
            .size = size,
        };
        ret = k_msgq_put(&audio_msgq, &block, K_NO_WAIT);
        if (ret != 0)
        {
            k_mem_slab_free(&app_audio_slab, buffer);
        }
    }
}
//把采集到的 I2S 原始数据，转换成电脑可以接收的 16 位 PCM 音频，再通过串口发出去。
static void audio_process_task(void *p1, void *p2, void *p3)
{
    while (1)
    {
        struct audio_block block;

        k_msgq_get(&audio_msgq, &block, K_FOREVER);

        #if AUDIO_UART_DEBUG
        uint32_t *slots = (uint32_t *)block.buffer;
        size_t count = block.size / sizeof(uint32_t);

        /* MAX9867 places its 16-bit sample at the MSB end of each 32-bit slot. */
        for (size_t i = 0; i < count; i++)
        {
            int32_t amplified = (int32_t)(int16_t)(slots[i] >> 16) * AUDIO_GAIN;

            if (amplified > INT16_MAX)
            {
                amplified = INT16_MAX;
            }
            else if (amplified < INT16_MIN)
            {
                amplified = INT16_MIN;
            }

            uint16_t sample = (uint16_t)(int16_t)amplified;

            /* WAV PCM is little-endian, independent of the MCU memory layout. */
            uart_poll_out(uart_dev, sample & 0xff);
            uart_poll_out(uart_dev, sample >> 8);
        }
        #endif
        k_mem_slab_free(&app_audio_slab, block.buffer);
    }
}

int main(void)
{
    int ret = controls_init();
    if (ret != 0)
    {
        printk("Button/LED init failed: %d\n",ret);
        return 0;
    }

    if (audio_init() != 0) 
    {
        printk("Audio init failed\n");
        return 0;
    }

    if (audio_start() != 0) 
    {
        printk("Audio start failed\n");
        return 0;
    }

    printk("Audio started\n");

    k_thread_create(&process_thread, process_stack, K_THREAD_STACK_SIZEOF(process_stack), audio_process_task, NULL, NULL, NULL, PROCESS_PRIORITY, 0, K_NO_WAIT);
    k_thread_create(&capture_thread, capture_stack, K_THREAD_STACK_SIZEOF(capture_stack), audio_capture_task, NULL, NULL, NULL, CAPTURE_PRIORITY, 0, K_NO_WAIT);

    return 0;
}
