#ifndef AUDIO_H
#define AUDIO_H

#include <stddef.h>
#include <stdint.h>

struct audio_sample
{
    int16_t *data;
    size_t size;
};

/**
 * @brief 初始化音频模块
 */
int audio_init(void);

/**
 * @brief 开始采集音频
 */
int audio_start(void);

/**
 * @brief 获取音频数据
 */
int audio_read(struct audio_sample *sample);


#endif