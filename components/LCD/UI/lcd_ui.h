#ifndef __LCD_UI_H
#define __LCD_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lcd_st7789.h"

/* ============================== LCD UI 可调参数区 ==============================
 *
 * 说明：
 *     1. 本头文件集中保存 Dashboard 页面布局和颜色参数，便于后期调试。
 *     2. UI 层只依赖 lcd_st7789.h 提供的基础绘图接口，不依赖 ENV/BME690/WiFi/ASR。
 *     3. 后续切换 LVGL 时，可保留 lcd_ui_dashboard_data_t 数据结构，替换本层绘制实现。
 */

/* LCD_UI_BG_COLOR：Dashboard 背景色。
 * 功能：
 *     每次刷新页面前用该颜色清屏。
 */
#define LCD_UI_BG_COLOR                         LCD_COLOR_BLACK

/* LCD_UI_TITLE_COLOR：标题文字颜色。
 * 功能：
 *     Dashboard 顶部标题使用该颜色显示。
 */
#define LCD_UI_TITLE_COLOR                      LCD_COLOR_CYAN

/* LCD_UI_LABEL_COLOR：左侧标签文字颜色。
 * 功能：
 *     TEMP/HUM/PRESS/GAS/WIFI/ASR 等标签统一使用该颜色。
 */
#define LCD_UI_LABEL_COLOR                      LCD_COLOR_GRAY

/* LCD_UI_VALUE_COLOR：普通数值文字颜色。
 * 功能：
 *     温度、湿度、气压默认数值颜色。
 */
#define LCD_UI_VALUE_COLOR                      LCD_COLOR_WHITE

/* LCD_UI_ACCENT_COLOR：重点数值文字颜色。
 * 功能：
 *     气体电阻等需要突出显示的指标使用该颜色。
 */
#define LCD_UI_ACCENT_COLOR                     LCD_COLOR_ORANGE

/* LCD_UI_STATE_ON_COLOR：状态为 ON/有效/已连接时的颜色。
 * 功能：
 *     WiFi connected、ASR active 或 ENV valid 等状态为 true 时使用。
 */
#define LCD_UI_STATE_ON_COLOR                   LCD_COLOR_GREEN

/* LCD_UI_STATE_OFF_COLOR：状态为 OFF/无效/未连接时的颜色。
 * 功能：
 *     WiFi/ASR 未连接或 ENV 数据无效时使用。
 */
#define LCD_UI_STATE_OFF_COLOR                  LCD_COLOR_RED

/* LCD_UI_LINE_COLOR：页面分割线颜色。
 * 功能：
 *     标题下方和状态区上方的细线使用该颜色。
 */
#define LCD_UI_LINE_COLOR                       LCD_COLOR_DARK_GRAY

/* LCD_UI_MARGIN_X：页面左右边距，单位像素。
 * 功能：
 *     控制所有文字和分割线距离屏幕边缘的水平距离。
 */
#define LCD_UI_MARGIN_X                         8

/* LCD_UI_TITLE_Y：标题文字 Y 坐标。
 * 功能：
 *     Dashboard 标题左上角纵坐标。
 */
#define LCD_UI_TITLE_Y                          6

/* LCD_UI_TITLE_X：标题文字 X 坐标。
 * 功能：
 *     Dashboard 标题左上角横坐标。
 */
#define LCD_UI_TITLE_X                          LCD_UI_MARGIN_X

/* LCD_UI_TITLE_LINE_Y：标题下方分割线 Y 坐标。
 * 功能：
 *     把标题区域和数据区域分隔开。
 */
#define LCD_UI_TITLE_LINE_Y                     28

/* LCD_UI_ROW_START_Y：第一行环境数据起始 Y 坐标。
 * 功能：
 *     TEMP 行从该纵坐标开始绘制。
 */
#define LCD_UI_ROW_START_Y                      38

/* LCD_UI_ROW_GAP：环境数据每行间距，单位像素。
 * 功能：
 *     控制 TEMP/HUM/PRESS/GAS 四行之间的垂直间隔。
 */
#define LCD_UI_ROW_GAP                          34

/* LCD_UI_LABEL_X：数据行标签 X 坐标。
 * 功能：
 *     所有数据行标签左对齐位置。
 */
#define LCD_UI_LABEL_X                          LCD_UI_MARGIN_X

/* LCD_UI_VALUE_X：数据行数值 X 坐标。
 * 功能：
 *     所有数据行数值左对齐位置。
 */
#define LCD_UI_VALUE_X                          92

/* LCD_UI_UNIT_X：数据行单位 X 坐标。
 * 功能：
 *     数据单位左对齐位置；需根据 font24 数值最大长度调整。
 */
#define LCD_UI_UNIT_X                           188

/* LCD_UI_STATUS_LINE_Y：状态区上方分割线 Y 坐标。
 * 功能：
 *     把环境数据区和 WiFi/ASR 状态区分隔开。
 */
#define LCD_UI_STATUS_LINE_Y                    178

/* LCD_UI_STATUS_Y：WiFi/ASR 状态文字 Y 坐标。
 * 功能：
 *     底部状态行左上角纵坐标。
 */
#define LCD_UI_STATUS_Y                         188

/* LCD_UI_STATUS_LEFT_X：WiFi 状态块 X 坐标。
 * 功能：
 *     WiFi 标签和状态值从该位置开始绘制。
 */
#define LCD_UI_STATUS_LEFT_X                    LCD_UI_MARGIN_X

/* LCD_UI_STATUS_RIGHT_X：ASR 状态块 X 坐标。
 * 功能：
 *     ASR 标签和状态值从该位置开始绘制。
 */
#define LCD_UI_STATUS_RIGHT_X                   122

/* LCD_UI_VALID_Y：ENV 数据有效性提示 Y 坐标。
 * 功能：
 *     底部第二行显示 ENV OK 或 WAIT。
 */
#define LCD_UI_VALID_Y                          214

/* LCD_UI_LINE_WIDTH：分割线宽度，单位像素。
 * 功能：
 *     水平分割线从左边距开始绘制到该宽度。
 */
#define LCD_UI_LINE_WIDTH                       (LCD_ST7789_WIDTH - (LCD_UI_MARGIN_X * 2))

/* LCD_UI_LINE_HEIGHT：分割线高度，单位像素。
 * 功能：
 *     当前使用 1 像素细线。
 */
#define LCD_UI_LINE_HEIGHT                      1

/* LCD_UI_TEMP_DECIMALS：温度显示小数位数。
 * 功能：
 *     默认显示 1 位小数，例如 25.6。
 */
#define LCD_UI_TEMP_DECIMALS                    1

/* LCD_UI_HUM_DECIMALS：湿度显示小数位数。
 * 功能：
 *     默认显示 1 位小数，例如 45.2。
 */
#define LCD_UI_HUM_DECIMALS                     1

/* LCD_UI_PRESS_DECIMALS：气压显示小数位数。
 * 功能：
 *     默认显示 1 位小数，例如 1013.2。
 */
#define LCD_UI_PRESS_DECIMALS                   1

/* LCD_UI_GAS_DECIMALS：气体电阻显示小数位数。
 * 功能：
 *     Dashboard 中气体电阻按整数显示。
 */
#define LCD_UI_GAS_DECIMALS                     0

/* LCD_UI_GAS_KOHM_DIVISOR：气体电阻 Ohm 转 kOhm 的换算系数。
 * 功能：
 *     屏幕空间有限，默认把 gas resistance 从 Ohm 转成 kOhm 显示。
 */
#define LCD_UI_GAS_KOHM_DIVISOR                 1000.0f

/* ============================== LCD UI 数据结构 ============================== */

typedef struct _lcd_ui_dashboard_data_t
{
    float temperature_c;              /* 温度，单位：摄氏度 */
    float humidity_percent;           /* 相对湿度，单位：%RH */
    float pressure_hpa;               /* 气压，单位：hPa */
    uint32_t gas_resistance_ohm;      /* 气体电阻，单位：Ohm */
    bool env_valid;                   /* 环境数据是否有效 */
    bool wifi_connected;              /* WiFi 是否已连接 */
    bool asr_active;                  /* ASR 是否处于激活/可用状态 */
} lcd_ui_dashboard_data_t;

/* ============================== LCD UI 对外接口声明 ============================== */

/* lcd_ui_draw_dashboard：绘制环境数据 Dashboard 页面。
 *
 * 功能：
 *     1. 清空 framebuffer；
 *     2. 绘制 temperature/humidity/pressure/gas resistance；
 *     3. 绘制 wifi state/asr state/env valid；
 *     4. 调用 lcd_st7789_flush() 通过 SPI DMA 刷屏。
 *
 * 参数：
 *     data：Dashboard 数据快照，不能为 NULL。
 *
 * 返回：
 *     ESP_OK：绘制并刷屏成功；
 *     ESP_ERR_INVALID_ARG：data 为 NULL；
 *     其它值：底层绘图或 SPI 刷屏失败。
 *
 * 调用方法：
 *     lcd_ui_dashboard_data_t data = {
 *         .temperature_c = 25.6f,
 *         .humidity_percent = 45.0f,
 *         .pressure_hpa = 1013.2f,
 *         .gas_resistance_ohm = 120000,
 *         .env_valid = true,
 *         .wifi_connected = true,
 *         .asr_active = false,
 *     };
 *     ESP_ERROR_CHECK(lcd_ui_draw_dashboard(&data));
 */
esp_err_t lcd_ui_draw_dashboard(const lcd_ui_dashboard_data_t *data);

#endif
