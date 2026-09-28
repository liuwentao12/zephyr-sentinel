#ifndef CONTROLS_H
#define CONTROLS_H

#include <stdbool.h>

/*
 * 消抖后，每次确认按下时调用一次。
 * 回调在系统工作队列中执行，只适合投递消息等短操作，不应阻塞等待。
 */
typedef void (*controls_button_handler_t)(void);

/**
 * @brief 初始化按键和录音指示灯，指示灯初始为熄灭状态。
 * @param on_pressed 按下事件回调，不能为空。
 * @return 成功返回 0，失败返回负错误码。
 *
 * 启动时调用一次；回调所需的资源必须在调用前准备好。
 */
int controls_init(controls_button_handler_t on_pressed);

/**
 * @brief 设置录音指示灯；仅更新灯的状态，不控制音频采集。
 * @param recording true 点亮，false 熄灭。
 * @return 成功返回 0，失败返回 GPIO 驱动的负错误码。
 *
 * 必须先成功调用 controls_init()。
 */
int controls_set_recording_led(bool recording);

#endif /* CONTROLS_H */
