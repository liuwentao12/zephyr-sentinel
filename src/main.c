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

enum control_cmd
{
    CMD_TOGGLE_RECORDING,
};

K_MSGQ_DEFINE(control_msgq, sizeof(enum control_cmd), 4, 4);
K_MSGQ_DEFINE(audio_msgq, sizeof(struct audio_block), APP_AUDIO_BLOCK_COUNT, 4);

K_SEM_DEFINE(audio_drained, 0, 1);

static struct gpio_callback button_cb;
static struct k_work_delayable button_debounce_work;

static void button_debounce_handler(struct k_work *work)
{
    static int last_pressed = 0;
    /*返回逻辑状态：1表示按下，0表示松开*/
    int pressed = gpio_pin_get_dt(&button);
    if (pressed < 0)
    {
        printk("Button read failed : %d\n",pressed);
        return;
    }
    /*状态没有变化，不产生新事件*/
    if (pressed == last_pressed)
    {
        return;
    }
    last_pressed = pressed;
    /*松开只更新状态，按下才发送指令*/
    if (pressed == 0)
    {
        return;
    }
    enum control_cmd cmd = CMD_TOGGLE_RECORDING;
    int ret = k_msgq_put(&control_msgq, &cmd, K_NO_WAIT);
    if (ret != 0)
    {
        printk("Control queue full: %d\n",ret);
    }
}

static void button_irq_handler(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
    k_work_reschedule(&button_debounce_work,K_MSEC(30));
}

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
    ret = gpio_pin_configure_dt(&record_led, GPIO_OUTPUT_INACTIVE);
    if (ret != 0)
    {
        return ret;
    }
    /*先准备好消抖工作和回调，再开启中断*/
    k_work_init_delayable(&button_debounce_work, button_debounce_handler);
    gpio_init_callback(&button_cb, button_irq_handler, BIT(button.pin));

    ret = gpio_add_callback(button.port, &button_cb);
    if (ret != 0)
    {
        return ret;
    }
    return gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
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
        
        ret = gpio_pin_set_dt(&record_led, 1);
        if (ret != 0)
        {
            printk("LED on failed: %d\n",ret);
        }
        
        printk("Audio started\n");

        //统计成功读取的音频块数量
        uint32_t blocks = 0;
        int read_error = 0;
        /*录音：检查停止指令，并持续采集*/
        while (1)
        {
            ret = k_msgq_get(&control_msgq, &cmd, K_NO_WAIT);
            if (ret == 0 && cmd == CMD_TOGGLE_RECORDING)
            {
                break;
            }
            
            void *buffer;
            /*有限等待，避免一直等内存而无法检查停止指令*/
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
                k_mem_slab_free(&app_audio_slab, buffer);
            }
            
            blocks++;
        }
        /*停止产生新的音频块*/
        ret = audio_stop();
        if (ret != 0)
        {
            printk("Audio stop failed: %d;reset required\n",ret);
            return;
        }
        /*结束标记排在已有音频块后*/
        struct audio_block end_marker =
        {
            .buffer = NULL,
            .size = 0,
        };
        ret = k_msgq_put(&audio_msgq, &end_marker, K_FOREVER);
        if (ret != 0)
        {
            printk("Audio end marker failed: %d; reset required\n",ret);
            return;
        }
        
        k_sem_take(&audio_drained, K_FOREVER);

        ret = gpio_pin_set_dt(&record_led, 0);
        if (ret != 0)
        {
            printk("LED off failed: %d\n",ret);
        }
        if (read_error != 0)
        {
            printk("Audio read failed:%d\n",read_error);
        }
        
        printk("Audio stopped, captured blocks: %u\n",(unsigned int)blocks);
    }
}
//消费音频块、处理结束标记，并按需输出串口调试音频
static void audio_process_task(void *p1, void *p2, void *p3)
{
    while (1)
    {
        struct audio_block block;

        k_msgq_get(&audio_msgq, &block, K_FOREVER);

        /*空指针作为结束标记，不代表真实的音频内存块*/
        if(block.buffer == NULL)
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

    printk("Ready: press SW2 to record\n");

    k_thread_create(&process_thread, process_stack, K_THREAD_STACK_SIZEOF(process_stack), audio_process_task, NULL, NULL, NULL, PROCESS_PRIORITY, 0, K_NO_WAIT);
    k_thread_create(&capture_thread, capture_stack, K_THREAD_STACK_SIZEOF(capture_stack), audio_capture_task, NULL, NULL, NULL, CAPTURE_PRIORITY, 0, K_NO_WAIT);

    return 0;
}
