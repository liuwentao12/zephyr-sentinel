#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/uart.h>

#include "audio.h"
#include "controls.h"
#include "display.h"

/* 0：关闭串口音频输出；1：开启串口音频输出 */
#define AUDIO_UART_DEBUG 0

/* 应用层音频缓冲，与 audio.c 中的 I2S 接收内存池分开管理。 */
#define APP_AUDIO_BLOCK_SIZE  512
#define APP_AUDIO_BLOCK_COUNT 4
#define AUDIO_GAIN 8

/* 采集线程负责 I2S 启停和读取，处理线程消费音频块。 */
#define CAPTURE_STACK_SIZE 1024
#define PROCESS_STACK_SIZE 1024

#define CAPTURE_PRIORITY 5
#define PROCESS_PRIORITY 7

K_THREAD_STACK_DEFINE(capture_stack, CAPTURE_STACK_SIZE);
K_THREAD_STACK_DEFINE(process_stack, PROCESS_STACK_SIZE);

static struct k_thread capture_thread;
static struct k_thread process_thread;

#if AUDIO_UART_DEBUG
static const struct device *uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#endif

K_MEM_SLAB_DEFINE_STATIC(app_audio_slab, APP_AUDIO_BLOCK_SIZE,
                         APP_AUDIO_BLOCK_COUNT, 4);

struct audio_block
{
    /* 入队成功后由处理线程释放；NULL 表示本次录音的结束标记。 */
    void *buffer;
    size_t size;
};

enum control_cmd
{
    CMD_TOGGLE_RECORDING,
};

/* 控制队列仅由采集线程接收，避免启停操作与音频读取并发。 */
K_MSGQ_DEFINE(control_msgq, sizeof(enum control_cmd), 4, 4);
K_MSGQ_DEFINE(audio_msgq, sizeof(struct audio_block), APP_AUDIO_BLOCK_COUNT, 4);

/* 处理线程收到结束标记后，通知采集线程：已归还本次录音的缓冲。 */
K_SEM_DEFINE(audio_drained, 0, 1);

/* controls 只报告按下事件，由应用层把事件解释为录音切换指令。 */
static void record_button_pressed(void)
{
    enum control_cmd cmd = CMD_TOGGLE_RECORDING;
    int ret = k_msgq_put(&control_msgq, &cmd, K_NO_WAIT);
    if (ret != 0)
    {
        printk("Control queue full: %d\n", ret);
    }
}

static void audio_capture_task(void *p1, void *p2, void *p3)
{
    while (1)
    {
        /* 待机：没有指令时阻塞，不读取音频。 */
        enum control_cmd cmd;

        int ret = k_msgq_get(&control_msgq, &cmd, K_FOREVER);
        if (ret != 0 || cmd != CMD_TOGGLE_RECORDING)
        {
            continue;
        }

        ret = audio_start();
        if (ret != 0)
        {
            continue;
        }

        ret = controls_set_recording_led(true);
        if (ret != 0)
        {
            printk("LED on failed: %d\n", ret);
        }

        printk("Audio started\n");

        /* 统计读取成功的块数，包含因应用队列已满而丢弃的块。 */
        uint32_t blocks = 0;
        int read_error = 0;
        /* 录音：检查停止指令，并持续采集。 */
        while (1)
        {
            ret = k_msgq_get(&control_msgq, &cmd, K_NO_WAIT);
            if (ret == 0 && cmd == CMD_TOGGLE_RECORDING)
            {
                break;
            }

            void *buffer;
            /* 有限等待，避免一直等内存而无法检查停止指令。 */
            ret = k_mem_slab_alloc(&app_audio_slab, &buffer, K_MSEC(20));
            if (ret != 0)
            {
                continue;
            }

            size_t size;
            ret = audio_read(buffer, APP_AUDIO_BLOCK_SIZE, &size);
            if (ret != 0)
            {
                k_mem_slab_free(&app_audio_slab, buffer);
                read_error = ret;
                break;
            }

            struct audio_block block =
            {
                .buffer = buffer,
                .size = size,
            };

            ret = k_msgq_put(&audio_msgq, &block, K_NO_WAIT);
            if (ret != 0)
            {
                /* 入队失败，缓冲仍归采集线程所有，必须在这里归还。 */
                k_mem_slab_free(&app_audio_slab, buffer);
            }

            blocks++;
        }
        /* 停止产生新的音频块。 */
        ret = audio_stop();
        if (ret != 0)
        {
            printk("Audio stop failed: %d;reset required\n", ret);
            return;
        }
        /* 结束标记排在已有音频块后，让处理线程按队列顺序完成收尾。 */
        struct audio_block end_marker =
        {
            .buffer = NULL,
            .size = 0,
        };
        ret = k_msgq_put(&audio_msgq, &end_marker, K_FOREVER);
        if (ret != 0)
        {
            printk("Audio end marker failed: %d; reset required\n", ret);
            return;
        }

        k_sem_take(&audio_drained, K_FOREVER);

        ret = controls_set_recording_led(false);
        if (ret != 0)
        {
            printk("LED off failed: %d\n", ret);
        }
        if (read_error != 0)
        {
            printk("Audio read failed:%d\n", read_error);
        }

        printk("Audio stopped, captured blocks: %u\n", (unsigned int)blocks);
    }
}

/* 消费音频块、处理结束标记，并按需输出串口调试音频。 */
static void audio_process_task(void *p1, void *p2, void *p3)
{
    while (1)
    {
        struct audio_block block;

        k_msgq_get(&audio_msgq, &block, K_FOREVER);

        /* 空指针作为结束标记，不代表真实的音频内存块，无需释放。 */
        if (block.buffer == NULL)
        {
            k_sem_give(&audio_drained);
            continue;
        }
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
    int ret = oled_init();

    if (ret == 0) {
        ret = oled_show_text("FUCK", "AUDIO READY");
    }

    if (ret != 0) {
        printk("OLED error: %d\n", ret);
    }

    ret = controls_init(record_button_pressed);
    if (ret != 0)
    {
        printk("Button/LED init failed: %d\n", ret);
        return 0;
    }

    if (audio_init() != 0)
    {
        printk("Audio init failed\n");
        return 0;
    }

    printk("Ready: press SW2 to record\n");

    /* 先准备消费者，再启动可能产生音频块的采集线程。 */
    k_thread_create(&process_thread, process_stack,
                    K_THREAD_STACK_SIZEOF(process_stack), audio_process_task,
                    NULL, NULL, NULL, PROCESS_PRIORITY, 0, K_NO_WAIT);
    k_thread_create(&capture_thread, capture_stack,
                    K_THREAD_STACK_SIZEOF(capture_stack), audio_capture_task,
                    NULL, NULL, NULL, CAPTURE_PRIORITY, 0, K_NO_WAIT);

    return 0;
}
