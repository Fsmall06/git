#ifndef UI_SENSOR_PAGE_H
#define UI_SENSOR_PAGE_H

#include <stdbool.h>

#include "esp_err.h"

/* ui_sensor_page_draw：绘制 Sensor 页面并立即刷新一次数据。
 *
 * 功能：
 *     1. 清屏并绘制 Sensor 页面静态内容，包括 Back 按键和 Sensor Data 标题；
 *     2. 静态内容绘制成功后，内部立即调用 ui_sensor_page_update(true) 强制刷新一次数据区域；
 *     3. 本接口只负责 Sensor 页面内部内容，不处理 HOME 页面、不处理页面状态切换。
 *
 * 调用方法：
 *     ui_main_switch_page(UI_PAGE_SENSOR);    // 页面切换到 Sensor 时由 ui_main.c 内部调用本函数
 *
 * 返回：
 *     ESP_OK：Sensor 页面绘制和首次数据刷新成功；
 *     其它值：LCD 绘图接口或 Sensor 数据区域刷新过程返回的错误码。
 */
esp_err_t ui_sensor_page_draw(void);

/* ui_sensor_page_update：刷新 Sensor 页面环境数据文本区域。
 *
 * 参数：
 *     force：
 *         true  表示强制刷新，不等待 UI_MAIN_SENSOR_REFRESH_MS 周期；
 *         false 表示普通周期刷新，未到 UI_MAIN_SENSOR_REFRESH_MS 周期时直接返回 ESP_OK。
 *
 * 功能：
 *     1. 使用 env_get_data(&data) 获取 ENV 模块缓存数据，不直接调用 BME690 底层驱动；
 *     2. 每次实际刷新前只覆盖 UI_MAIN_SENSOR_CLEAR_* 定义的数据文本区域，避免整屏闪烁；
 *     3. ENV 数据未准备好时显示 UI_MAIN_SENSOR_NOT_READY_TEXT，不阻塞、不崩溃。
 *
 * 调用方法：
 *     ui_sensor_page_update(true);     // 进入 Sensor 页面后需要立即显示数据时调用
 *     ui_sensor_page_update(false);    // UI 主循环中、当前页面为 Sensor 时周期调用
 *
 * 返回：
 *     ESP_OK：未到刷新周期或刷新成功；
 *     其它值：LCD 局部覆盖或文本绘制返回的错误码。
 */
esp_err_t ui_sensor_page_update(bool force);

/* ui_sensor_page_is_back_hit：判断触摸点是否命中 Sensor 页面的 Back 按键。
 *
 * 参数：
 *     x：已经由 ui_main.c 映射后的 LCD X 坐标；
 *     y：已经由 ui_main.c 映射后的 LCD Y 坐标。
 *
 * 功能：
 *     只判断坐标是否落入 UI_MAIN_BACK_BTN_* 定义的 Back 按键矩形区域，不读取触摸底层，
 *     不切换页面，也不刷新 Sensor 数据。
 *
 * 调用方法：
 *     if (ui_sensor_page_is_back_hit(x, y)) {
 *         ui_main_switch_page(UI_PAGE_HOME);
 *     }
 *
 * 返回：
 *     true：触摸点命中 Back 按键；
 *     false：触摸点未命中 Back 按键。
 */
bool ui_sensor_page_is_back_hit(int x, int y);

#endif
