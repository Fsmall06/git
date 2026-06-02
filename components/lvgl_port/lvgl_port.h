#ifndef __LVGL_PORT_H
#define __LVGL_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl_port_config.h"

/* lvgl_port_init：初始化 LVGL 核心、显示适配、触摸适配和 LVGL tick。
 *
 * 功能：
 *     1. 调用 lv_init() 初始化 LVGL；
 *     2. 创建 LVGL 绘图缓冲区；
 *     3. 注册 lvgl_flush_cb()，刷新时调用现有 LCD 对外绘图接口；
 *     4. 注册 lvgl_touch_read_cb()，读取时调用现有 cst816t_read_point()；
 *     5. 创建 esp_timer，周期调用 lv_tick_inc()。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_init());
 *     ESP_ERROR_CHECK(cst816t_init());
 *     ESP_ERROR_CHECK(lvgl_port_init());
 *
 * 返回：
 *     ESP_OK：初始化成功或已经初始化；
 *     ESP_ERR_NO_MEM：绘图缓冲区、互斥锁或定时器资源申请失败；
 *     其它值：底层 esp_timer 等接口返回的错误码。
 */
esp_err_t lvgl_port_init(void);

/* lvgl_port_start：启动 LVGL 周期处理任务。
 *
 * 功能：
 *     创建 FreeRTOS 任务，周期调用 lv_timer_handler()，驱动页面刷新和触摸事件派发。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lvgl_port_init());
 *     ui_main_create();
 *     ESP_ERROR_CHECK(lvgl_port_start());
 *
 * 返回：
 *     ESP_OK：任务启动成功或已经启动；
 *     ESP_ERR_INVALID_STATE：尚未调用 lvgl_port_init()；
 *     ESP_ERR_NO_MEM：任务创建失败。
 */
esp_err_t lvgl_port_start(void);

/* lvgl_port_lock：在非 LVGL 任务中访问 LVGL API 前加锁。
 *
 * 参数：
 *     timeout_ms：等待互斥锁的最长时间，单位 ms。
 *
 * 返回：
 *     true：加锁成功，可以安全调用 LVGL API；
 *     false：加锁失败，本次不应继续访问 LVGL API。
 *
 * 调用方法：
 *     if (lvgl_port_lock(LVGL_PORT_LOCK_TIMEOUT_MS)) {
 *         ui_main_create();
 *         lvgl_port_unlock();
 *     }
 */
bool lvgl_port_lock(uint32_t timeout_ms);

/* lvgl_port_unlock：释放 lvgl_port_lock() 获取的 LVGL 互斥锁。
 *
 * 调用方法：
 *     lvgl_port_lock(LVGL_PORT_LOCK_TIMEOUT_MS);
 *     ui_main_create();
 *     lvgl_port_unlock();
 */
void lvgl_port_unlock(void);

#endif
