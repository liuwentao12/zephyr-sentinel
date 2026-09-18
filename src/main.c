#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>

#include "audio.h"

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

static struct k_thread capture_thread;
static struct k_thread process_thread;

static const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

K_MEM_SLAB_DEFINE_STATIC(app_audio_slab, APP_AUDIO_BLOCK_SIZE, APP_AUDIO_BLOCK_COUNT,4);

struct audio_block
{
    void *buffer;
    size_t size;
};
//消息队列
K_MSGQ_DEFINE(audio_msgq, sizeof(struct audio_block), APP_AUDIO_BLOCK_COUNT, 4);


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

static void audio_process_task(void *p1, void *p2, void *p3)
{
    while (1)
    {
        struct audio_block block;

        k_msgq_get(&audio_msgq, &block, K_FOREVER);

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

        k_mem_slab_free(&app_audio_slab, block.buffer);
    }
}

int main(void)
{
    if (audio_init() != 0) {
        printk("Audio init failed\n");
        return 0;
    }

    if (audio_start() != 0) {
        printk("Audio start failed\n");
        return 0;
    }

    printk("Audio started\n");

    k_thread_create(&process_thread, process_stack, K_THREAD_STACK_SIZEOF(process_stack), audio_process_task, NULL, NULL, NULL, PROCESS_PRIORITY, 0, K_NO_WAIT);
    k_thread_create(&capture_thread, capture_stack, K_THREAD_STACK_SIZEOF(capture_stack), audio_capture_task, NULL, NULL, NULL, CAPTURE_PRIORITY, 0, K_NO_WAIT);

    return 0;
}
