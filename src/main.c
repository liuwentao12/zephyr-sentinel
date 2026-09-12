#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "audio.h"

#define AUDIO_BUFFER_SIZE 512
static int16_t audio_buffer[AUDIO_BUFFER_SIZE / sizeof(int16_t)];

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

    int64_t last_print = 0;
    while (1)
    {
        size_t size;
        int ret = audio_read(audio_buffer, sizeof(audio_buffer), &size);

        if (ret != 0) {
            printk("Audio read failed: %d\n", ret);
            break;
        }

        size_t count = size / sizeof(int16_t);

        int16_t min = audio_buffer[1];
        int16_t max = audio_buffer[1];

        /* 只看右声道：1, 3, 5, 7... */
        for (size_t i = 1; i < count; i += 2)
        {
            int16_t value = audio_buffer[i];

            if (value < min)
                min = value;

            if (value > max)
                max = value;
        }

        int volume = (int)max - (int)min;

        if (k_uptime_get() - last_print >= 500)
        {
            printk("volume=%d\n", volume);
            last_print = k_uptime_get();
        }
    }

    return 0;
}