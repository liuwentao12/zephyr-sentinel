#ifndef AUDIO_H
#define AUDIO_H

#include <stddef.h>
#include <stdint.h>

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
int audio_read(int16_t *buffer, size_t buffer_size, size_t *read_size);


#endif