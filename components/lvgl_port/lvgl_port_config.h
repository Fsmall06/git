#ifndef __LVGL_PORT_CONFIG_H
#define __LVGL_PORT_CONFIG_H

/*
 * LVGL 适配层可调参数集中区
 *
 * 说明：
 *     1. 本文件只保存 LVGL 适配层后续可能需要调试的参数。
 *     2. 如果后期需要调整任务栈、刷新缓冲行数、触摸映射范围或 LVGL 调度周期，
 *        优先修改这里的宏定义，不直接改 lvgl_port.c 的业务代码。
 *     3. 本适配层只依赖现有 LCD 对外接口和 CST816T 对外接口，不修改 LCD/CST816T 驱动源码，
 *        不访问 BME690、ENV、WiFi 等业务模块。
 */

/* LVGL_PORT_LOG_TAG：LVGL 适配层日志 TAG。
 *
 * 功能：
 *     ESP_LOGI/ESP_LOGE 打印时使用该 TAG，便于串口过滤 LVGL 适配层日志。
 *
 * 调用方法：
 *     lvgl_port_init();   // 内部日志会使用该 TAG
 */
#define LVGL_PORT_LOG_TAG                       "LVGL_PORT"

/* LVGL_PORT_TASK_NAME：LVGL 定时处理任务名称。
 *
 * 功能：
 *     lvgl_port_start() 创建 FreeRTOS 任务时使用，便于在任务列表或调试器中识别。
 */
#define LVGL_PORT_TASK_NAME                     "lvgl_task"

/* LVGL_PORT_TASK_STACK_SIZE：LVGL 任务栈大小，单位 Byte。
 *
 * 功能：
 *     LVGL 任务会周期调用 lv_timer_handler()，并执行按钮事件回调。
 *
 * 后期调试：
 *     如果后续 UI 控件增多、字体变大或事件逻辑增加，可适当增大该值。
 */
#define LVGL_PORT_TASK_STACK_SIZE               6144U

/* LVGL_PORT_TASK_PRIORITY：LVGL 任务优先级。
 *
 * 功能：
 *     控制 LVGL 刷新和触摸事件处理的调度优先级。
 *
 * 后期调试：
 *     如果触摸响应迟缓，可略微提高；如果影响传感器采样，可降低。
 */
#define LVGL_PORT_TASK_PRIORITY                 5U

/* LVGL_PORT_TICK_PERIOD_MS：LVGL 系统 tick 周期，单位 ms。
 *
 * 功能：
 *     esp_timer 会按该周期调用 lv_tick_inc()，为 LVGL 动画、输入和定时器提供时间基准。
 *
 * 调用方法：
 *     lvgl_port_init();   // 内部自动创建并启动 tick 定时器
 */
#define LVGL_PORT_TICK_PERIOD_MS                5U

/* LVGL_PORT_TASK_PERIOD_MS：LVGL 任务兜底调度周期，单位 ms。
 *
 * 功能：
 *     lv_timer_handler() 返回时间异常时，任务至少按该周期继续运行。
 */
#define LVGL_PORT_TASK_PERIOD_MS                10U

/* LVGL_PORT_TASK_MAX_DELAY_MS：LVGL 任务最大休眠时间，单位 ms。
 *
 * 功能：
 *     限制 lv_timer_handler() 返回的下次调度时间，避免输入读取间隔过长。
 */
#define LVGL_PORT_TASK_MAX_DELAY_MS             20U

/* LVGL_PORT_LOCK_TIMEOUT_MS：业务代码访问 LVGL API 时的默认加锁超时，单位 ms。
 *
 * 功能：
 *     app_main 创建页面时调用 lvgl_port_lock() 使用该超时，避免 LVGL 任务并发访问对象树。
 *
 * 调用方法：
 *     if (lvgl_port_lock(LVGL_PORT_LOCK_TIMEOUT_MS)) {
 *         ui_main_create();
 *         lvgl_port_unlock();
 *     }
 */
#define LVGL_PORT_LOCK_TIMEOUT_MS               1000U

/* LVGL_PORT_DISP_BUFFER_LINES：LVGL 绘图缓冲区行数。
 *
 * 功能：
 *     控制 LVGL 单缓冲区大小，实际像素数为 LCD_H_RES * LVGL_PORT_DISP_BUFFER_LINES。
 *
 * 后期调试：
 *     数值越大，LVGL 分块刷新次数越少，但 RAM 占用越高。
 */
#define LVGL_PORT_DISP_BUFFER_LINES             20U

/* LVGL_PORT_TOUCH_RAW_INVALID_LIMIT：触摸原始坐标无效阈值。
 *
 * 功能：
 *     CST816T 读到接近 4095 的异常值时视为无效点，与已验证 touch_paint 逻辑保持一致。
 */
#define LVGL_PORT_TOUCH_RAW_INVALID_LIMIT       4090U

/* LVGL_PORT_TOUCH_RAW_X_MIN / LVGL_PORT_TOUCH_RAW_X_MAX：触摸 X 原始坐标标定范围。
 *
 * 功能：
 *     将 cst816t_read_point() 输出的 raw_x 映射到 LCD 0 ~ LCD_H_RES-1。
 *
 * 重要：
 *     当前数值来自已验证通过的 touch_paint 坐标映射逻辑，请勿随意修改。
 */
#define LVGL_PORT_TOUCH_RAW_X_MIN               14U
#define LVGL_PORT_TOUCH_RAW_X_MAX               226U

/* LVGL_PORT_TOUCH_RAW_Y_MIN / LVGL_PORT_TOUCH_RAW_Y_MAX：触摸 Y 原始坐标标定范围。
 *
 * 功能：
 *     将 cst816t_read_point() 输出的 raw_y 映射到 LCD 0 ~ LCD_V_RES-1。
 *
 * 重要：
 *     当前数值来自已验证通过的 touch_paint 坐标映射逻辑，请勿随意修改。
 */
#define LVGL_PORT_TOUCH_RAW_Y_MIN               35U
#define LVGL_PORT_TOUCH_RAW_Y_MAX               271U

/* LVGL_PORT_TOUCH_INVERT_Y：是否翻转映射后的 Y 坐标。
 *
 * 功能：
 *     1 表示 y = LCD_V_RES - 1 - y，与已验证 touch_paint 映射逻辑一致；
 *     0 表示不翻转 Y 坐标。
 */
#define LVGL_PORT_TOUCH_INVERT_Y                1

#endif
