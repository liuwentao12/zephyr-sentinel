#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

/* 初始化 OLED，仅调用一次 */
int oled_init(void);

/* 在 OLED 上显示两行文字 */
int oled_show_text(const char *line1,const char *line2);

#endif