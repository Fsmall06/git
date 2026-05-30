#include "lcd_st7789.h"

/* g_lcd_font24：16x24 ASCII 字体对象。
 *
 * 功能：
 *     用于 Dashboard 主要数值显示。该字体不重复保存点阵数据，而是复用 font16.c 中的
 *     lcd_font_get_basic_5x7_glyph()，通过 2x3 缩放得到更大的显示效果。
 *
 * 调用方法：
 *     lcd_st7789_draw_number(96, 32, 25.6f, 1, &g_lcd_font24, LCD_COLOR_CYAN, LCD_COLOR_BLACK);
 *
 * 移植说明：
 *     后续如果替换成专用 16x24 字库，可以保持 g_lcd_font24 符号不变，只改本文件内部实现。
 */
const lcd_font_t g_lcd_font24 = {
    .width = LCD_FONT24_WIDTH,
    .height = LCD_FONT24_HEIGHT,
    .base_width = LCD_FONT_BASE_WIDTH,
    .base_height = LCD_FONT_BASE_HEIGHT,
    .scale_x = LCD_FONT24_SCALE_X,
    .scale_y = LCD_FONT24_SCALE_Y,
    .offset_x = LCD_FONT24_OFFSET_X,
    .offset_y = LCD_FONT24_OFFSET_Y,
    .get_glyph = lcd_font_get_basic_5x7_glyph,
};
