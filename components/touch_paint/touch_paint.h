#ifndef __TOUCH_PAINT_H
#define __TOUCH_PAINT_H

#include <stdint.h>

#include "esp_err.h"
#include "lcd.h"

/* ============================== Touch Paint 可调参数区 ==============================
 *
 * 说明：
 *     1. 本头文件集中保存触摸画板后续调试可能要改动的参数。
 *     2. 后期如果要调整画点大小、任务周期、任务栈、标题位置或颜色，优先修改本文件宏定义。
 *     3. Touch Paint 模块只依赖 LCD 绘图接口和 CST816T 触摸读取接口，不依赖 BME690、ENV、WiFi、LLM 等业务模块。
 */

/* TOUCH_PAINT_TASK_NAME：触摸画板 FreeRTOS 任务名称。
 *
 * 功能：
 *     创建任务时用于标识 touch_paint_task()，便于日志、调试器或任务列表中查找。
 *
 * 调用方法：
 *     touch_paint_start();    // 内部会使用该名称创建 touch_paint_task()
 */
#define TOUCH_PAINT_TASK_NAME                  "touch_paint_task"

/* TOUCH_PAINT_TASK_STACK_SIZE：触摸画板任务栈大小，单位 Byte。
 *
 * 功能：
 *     touch_paint_task() 会周期读取触摸坐标并调用 LCD 绘图接口，默认 4096 Byte 预留较充足。
 *
 * 后期调试：
 *     如果增加更复杂的绘图逻辑或日志内容，可适当增大该值。
 */
#define TOUCH_PAINT_TASK_STACK_SIZE            4096U

/* TOUCH_PAINT_TASK_PRIORITY：触摸画板任务优先级。
 *
 * 功能：
 *     控制触摸绘图任务的调度优先级。当前绘图属于人机交互任务，默认与触摸轮询同级。
 *
 * 后期调试：
 *     如果触摸轨迹明显断续，可略微提高；如果影响传感器采样或其它任务，可降低。
 */
#define TOUCH_PAINT_TASK_PRIORITY              5U

/* TOUCH_PAINT_PERIOD_MS：触摸画板任务循环周期，单位 ms。
 *
 * 功能：
 *     touch_paint_task() 每隔该时间读取一次 cst816t_read_point()。
 *
 * 当前要求：
 *     固定为 20ms，用于获得较连续的手指轨迹。
 */
#define TOUCH_PAINT_PERIOD_MS                  20U

/* TOUCH_PAINT_LOG_PERIOD_MS：触摸坐标日志限流周期，单位 ms。
 *
 * 功能：
 *     按下并移动时最多每隔该时间打印一次 TOUCH 坐标，避免 20ms 轮询刷爆串口。
 *
 * 日志格式：
 *     TOUCH:
 *     x=%u
 *     y=%u
 */
#define TOUCH_PAINT_LOG_PERIOD_MS              500U

/* TOUCH_PAINT_POINT_RADIUS：红色实心点半径，单位像素。
 *
 * 功能：
 *     绘图时以触摸坐标为中心画实心方点。默认半径 2，因此最终点大小为 5x5。
 *
 * 边界保护：
 *     当 x < TOUCH_PAINT_POINT_RADIUS 或 y < TOUCH_PAINT_POINT_RADIUS 时不绘制，
 *     避免计算 x - 2、y - 2 时发生无符号下溢。
 */
#define TOUCH_PAINT_POINT_RADIUS               2U

/* TOUCH_PAINT_POINT_SIZE：红色实心点边长，单位像素。
 *
 * 功能：
 *     lcd_fill_rect() 的 width/height 参数，默认 2 * radius + 1，也就是 5。
 *
 * 调用方法：
 *     lcd_fill_rect(x - TOUCH_PAINT_POINT_RADIUS,
 *                   y - TOUCH_PAINT_POINT_RADIUS,
 *                   TOUCH_PAINT_POINT_SIZE,
 *                   TOUCH_PAINT_POINT_SIZE,
 *                   TOUCH_PAINT_POINT_COLOR);
 */
#define TOUCH_PAINT_POINT_SIZE                 ((TOUCH_PAINT_POINT_RADIUS * 2U) + 1U)

/* TOUCH_PAINT_POINT_COLOR：手指轨迹颜色。
 *
 * 功能：
 *     设置触摸按下时绘制的实心点颜色，当前要求为红色。
 */
#define TOUCH_PAINT_POINT_COLOR                LCD_COLOR_RED

/* TOUCH_PAINT_BACKGROUND_COLOR：画板背景色。
 *
 * 功能：
 *     touch_paint_start() 启动时会调用 lcd_clear() 清屏为该颜色。
 */
#define TOUCH_PAINT_BACKGROUND_COLOR           LCD_COLOR_BLACK

/* TOUCH_PAINT_TITLE_TEXT：屏幕顶部标题文字。
 *
 * 功能：
 *     启动画板时显示在屏幕顶部，用于确认当前运行的是触摸画板 Demo。
 */
#define TOUCH_PAINT_TITLE_TEXT                 "Touch Paint Demo"

/* TOUCH_PAINT_TITLE_X / TOUCH_PAINT_TITLE_Y：标题左上角坐标。
 *
 * 功能：
 *     控制 Touch Paint Demo 标题在 LCD 上的显示位置。
 *
 * 后期调试：
 *     如果更换字体大小或屏幕方向后标题不居中，可优先调整这两个宏。
 */
#define TOUCH_PAINT_TITLE_X                    24U
#define TOUCH_PAINT_TITLE_Y                    8U

/* TOUCH_PAINT_TITLE_COLOR：标题前景色。
 *
 * 功能：
 *     当前要求标题为白色。
 */
#define TOUCH_PAINT_TITLE_COLOR                LCD_COLOR_WHITE

/* TOUCH_PAINT_TITLE_BACKGROUND_COLOR：标题背景色。
 *
 * 功能：
 *     与画板背景保持一致，避免标题字符块出现不同底色。
 */
#define TOUCH_PAINT_TITLE_BACKGROUND_COLOR     TOUCH_PAINT_BACKGROUND_COLOR

/* TOUCH_PAINT_LOG_TAG：触摸画板日志 TAG。
 *
 * 功能：
 *     用于 ESP_LOGI/ESP_LOGE 的模块标识，方便串口日志过滤。
 */
#define TOUCH_PAINT_LOG_TAG                    "TOUCH_PAINT"

/* ============================== 对外函数声明 ============================== */

/* touch_paint_start：启动 LCD 触摸画板 Demo。
 *
 * 功能：
 *     1. 清屏为黑色；
 *     2. 在屏幕顶部显示 "Touch Paint Demo"；
 *     3. 创建 touch_paint_task()，周期读取 CST816T 坐标并在 LCD 上绘制红色轨迹；
 *     4. 如果重复调用且任务已经运行，直接返回 ESP_OK，不重复创建任务。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_init());
 *     ESP_ERROR_CHECK(cst816t_init());
 *     ESP_ERROR_CHECK(touch_paint_start());
 *
 * 返回：
 *     ESP_OK：启动成功或任务已经运行；
 *     ESP_ERR_NO_MEM：FreeRTOS 任务创建失败；
 *     其它值：LCD 清屏或标题绘制失败时返回对应错误码。
 */
esp_err_t touch_paint_start(void);

/* touch_paint_task：LCD 触摸画板任务函数。
 *
 * 功能：
 *     1. 每 TOUCH_PAINT_PERIOD_MS 调用一次 cst816t_read_point()；
 *     2. pressed=true 时，在 LCD 上绘制红色实心点；
 *     3. 如果上一次按下坐标有效，会在两点之间补点，形成连续轨迹；
 *     4. 不清屏，保留历史轨迹；
 *     5. TOUCH 坐标日志最多每 TOUCH_PAINT_LOG_PERIOD_MS 打印一次。
 *
 * 调用方法：
 *     推荐调用 touch_paint_start() 间接创建任务，不建议业务代码直接调用本函数。
 */
void touch_paint_task(void *arg);

#endif
