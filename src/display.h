#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

/* 频谱显示模式 */
typedef enum {
    DISPLAY_MODE_16 = 16,
    DISPLAY_MODE_32 = 32
} display_mode_t;

/* 初始化显示设备 */
int app_display_init(void);

/* 绘制静态界面 */
int app_display_draw_layout(display_mode_t mode);

#endif /* APP_DISPLAY_H */
