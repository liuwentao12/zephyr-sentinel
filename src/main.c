#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "audio.h"


int main(void)
{
    struct audio_sample sample = {0};

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
        int ret = audio_read(&sample);

        if (ret != 0) {
            printk("Audio read failed: %d\n", ret);
            break;
        }

        size_t count = sample.size / sizeof(int16_t);

        int16_t min = sample.data[1];
        int16_t max = sample.data[1];

        /* 只看右声道：1, 3, 5, 7... */
        for (size_t i = 1; i < count; i += 2)
        {
            int16_t value = sample.data[i];

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