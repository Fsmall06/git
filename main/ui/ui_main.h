#ifndef UI_MAIN_H
#define UI_MAIN_H

#include <stdint.h>

#include "esp_err.h"
#include "lcd.h"

/* ============================== UI 主界面可调参数区 ==============================
 *
 * 说明：
 *     1. 本头文件集中保存 HOME/Sensor 页面 UI 后续调试可能需要修改的全部参数。
 *     2. 如需调整任务周期、触摸校准、按键坐标、页面文字或颜色，优先修改本文件宏定义。
 *     3. UI 模块只使用 LCD 绘图接口、CST816T 触摸读取接口，以及 Sensor 页面刷新函数中的 ENV getter；
 *        不直接依赖 BME690/WiFi 等底层或业务模块，这样后续移植 UI 页面时可以保持相对独立。
 */

/* UI_MAIN_TASK_NAME：UI 触摸任务名称。
 *
 * 功能：
 *     ui_main_start() 创建 FreeRTOS 任务时使用该名称，便于串口日志和调试工具识别任务。
 *
 * 调用方法：
 *     ui_main_start();    // 内部会使用 UI_MAIN_TASK_NAME 创建 UI 任务
 */
#define UI_MAIN_TASK_NAME                    "ui_task"

/* UI_MAIN_TASK_STACK_SIZE：UI 触摸任务栈大小，单位 Byte。
 *
 * 功能：
 *     控制 ui_main_task() 的任务栈空间。当前 UI 只进行触摸轮询和 LCD 简单绘图，4096 Byte 预留充足。
 *
 * 后续调试：
 *     如果后续 Sensor 页面增加更多字符串格式化或复杂绘图，可优先适当增大该值。
 */
#define UI_MAIN_TASK_STACK_SIZE              4096U

/* UI_MAIN_TASK_PRIORITY：UI 触摸任务优先级。
 *
 * 功能：
 *     控制 UI 任务在 FreeRTOS 中的调度优先级。当前用于 HOME/Sensor 页面触摸分发和刷新调度。
 */
#define UI_MAIN_TASK_PRIORITY                5U

/* UI_MAIN_POLL_PERIOD_MS：UI 触摸轮询周期，单位 ms。
 *
 * 功能：
 *     ui_main_task() 每隔该时间调用一次 cst816t_read_point() 读取触摸状态。
 *
 * 调用方法：
 *     vTaskDelay(ui_main_ms_to_ticks(UI_MAIN_POLL_PERIOD_MS));
 */
#define UI_MAIN_POLL_PERIOD_MS               20U

/* UI_MAIN_TOUCH_X_MIN / UI_MAIN_TOUCH_X_MAX：触摸原始 X 坐标校准范围。
 *
 * 功能：
 *     ui_main_map_touch_point() 使用这两个宏把 CST816T 原始 X 坐标映射到 LCD 像素坐标。
 *
 * 后续调试：
 *     如果点击位置与屏幕显示位置左右偏移，可优先调整这两个值。
 */
#define UI_MAIN_TOUCH_X_MIN                  14
#define UI_MAIN_TOUCH_X_MAX                  226

/* UI_MAIN_TOUCH_Y_MIN / UI_MAIN_TOUCH_Y_MAX：触摸原始 Y 坐标校准范围。
 *
 * 功能：
 *     ui_main_map_touch_point() 使用这两个宏把 CST816T 原始 Y 坐标映射到 LCD 像素坐标。
 *
 * 后续调试：
 *     如果点击位置与屏幕显示位置上下偏移，可优先调整这两个值。
 */
#define UI_MAIN_TOUCH_Y_MIN                  35
#define UI_MAIN_TOUCH_Y_MAX                  271

/* UI_MAIN_HOME_TITLE_X / UI_MAIN_HOME_TITLE_Y：HOME 页面标题坐标。
 *
 * 功能：
 *     控制 HOME 页面标题在屏幕上的显示位置。
 */
#define UI_MAIN_HOME_TITLE_X                 42U
#define UI_MAIN_HOME_TITLE_Y                 28U

/* UI_MAIN_HOME_TITLE_TEXT：HOME 页面标题文字。
 *
 * 功能：
 *     控制 HOME 页面顶部标题内容，当前显示 "Sensor Demo"。
 */
#define UI_MAIN_HOME_TITLE_TEXT              "Sensor Demo"

/* UI_MAIN_SENSOR_BTN_X / UI_MAIN_SENSOR_BTN_Y：HOME 页面居中 Sensor 入口按键左上角坐标。
 *
 * 功能：
 *     控制 HOME 页面中间 "Sensor" 大按键的位置。当前屏幕为 240x284，
 *     按键尺寸为 140x60，左上角坐标为 (50, 120)，用于让入口按键位于屏幕中间区域。
 *
 * 调用方法：
 *     点击该矩形区域后，UI 会调用 ui_main_switch_page(UI_PAGE_SENSOR) 进入 Sensor 页面。
 */
#define UI_MAIN_SENSOR_BTN_X                 50U
#define UI_MAIN_SENSOR_BTN_Y                 120U

/* UI_MAIN_SENSOR_BTN_W / UI_MAIN_SENSOR_BTN_H：HOME 页面居中 Sensor 入口按键尺寸，单位像素。
 *
 * 功能：
 *     控制 HOME 页面唯一入口 "Sensor" 大按键的宽度和高度。
 */
#define UI_MAIN_SENSOR_BTN_W                 140U
#define UI_MAIN_SENSOR_BTN_H                 60U

/* UI_MAIN_SENSOR_BTN_TEXT_X / UI_MAIN_SENSOR_BTN_TEXT_Y：Sensor 按键文字坐标。
 *
 * 功能：
 *     控制 "Sensor" 文字在入口按键中的显示位置。
 */
#define UI_MAIN_SENSOR_BTN_TEXT_X            84U
#define UI_MAIN_SENSOR_BTN_TEXT_Y            143U

/* UI_MAIN_SENSOR_BTN_TEXT：Sensor 入口按键文字。
 *
 * 功能：
 *     控制 HOME 页面居中 Sensor 入口按键显示的文字内容。
 */
#define UI_MAIN_SENSOR_BTN_TEXT              "Sensor"

/* UI_MAIN_BACK_BTN_X / UI_MAIN_BACK_BTN_Y：Back 按键左上角坐标。
 *
 * 功能：
 *     控制 Sensor 页面左上角 "Back" 返回按键的位置。
 *
 * 调用方法：
 *     点击该矩形区域后，UI 会调用 ui_main_switch_page(UI_PAGE_HOME) 返回 HOME 主界面。
 */
#define UI_MAIN_BACK_BTN_X                   0U
#define UI_MAIN_BACK_BTN_Y                   0U

/* UI_MAIN_BACK_BTN_W / UI_MAIN_BACK_BTN_H：Back 按键尺寸，单位像素。
 *
 * 功能：
 *     控制 Sensor 页面左上角 "Back" 返回按键的宽度和高度。
 */
#define UI_MAIN_BACK_BTN_W                   70U
#define UI_MAIN_BACK_BTN_H                   40U

/* UI_MAIN_BACK_BTN_TEXT_X / UI_MAIN_BACK_BTN_TEXT_Y：Back 按键文字坐标。
 *
 * 功能：
 *     控制 "Back" 文字在返回按键中的显示位置。
 */
#define UI_MAIN_BACK_BTN_TEXT_X              12U
#define UI_MAIN_BACK_BTN_TEXT_Y              13U

/* UI_MAIN_BACK_BTN_TEXT：Back 返回按键文字。
 *
 * 功能：
 *     控制 Sensor 页面左上角返回按键显示的文字内容。
 */
#define UI_MAIN_BACK_BTN_TEXT                "Back"

/* UI_MAIN_SENSOR_TITLE_X / UI_MAIN_SENSOR_TITLE_Y：Sensor 页面标题坐标。
 *
 * 功能：
 *     控制 "Sensor Data" 标题在 Sensor 页面上的显示位置。
 */
#define UI_MAIN_SENSOR_TITLE_X               54U
#define UI_MAIN_SENSOR_TITLE_Y               60U

/* UI_MAIN_SENSOR_TITLE_TEXT：Sensor 页面标题文字。
 *
 * 功能：
 *     控制 Sensor 页面标题内容。标题只负责静态显示，真实环境数据由数据刷新区域单独绘制。
 */
#define UI_MAIN_SENSOR_TITLE_TEXT            "Sensor Data"

/* UI_MAIN_SENSOR_REFRESH_MS：Sensor 页面环境数据刷新周期，单位 ms。
 *
 * 功能：
 *     控制 ui_sensor_page_update(false) 在 Sensor 页面中多久从 ENV 缓存读取并重绘一次数据。
 */
#define UI_MAIN_SENSOR_REFRESH_MS            1000U

/* UI_MAIN_SENSOR_DATA_X / UI_MAIN_SENSOR_DATA_Y：Sensor 页面环境数据文本起始坐标。
 *
 * 功能：
 *     控制 Temperature、Humidity、Pressure、Gas 四行数据第一行左上角的显示位置。
 */
#define UI_MAIN_SENSOR_DATA_X                8U
#define UI_MAIN_SENSOR_DATA_Y                112U

/* UI_MAIN_SENSOR_LINE_GAP：Sensor 页面环境数据文本行间距，单位像素。
 *
 * 功能：
 *     控制四行环境数据相邻两行左上角 Y 坐标之间的距离，后续调试显示疏密时修改本宏。
 */
#define UI_MAIN_SENSOR_LINE_GAP              28U

/* UI_MAIN_SMALL_FONT_WIDTH：Sensor 数据区小号 ASCII 字体点阵宽度，单位点阵列。
 *
 * 功能：
 *     控制 UI 内部小号字体每个字符的点阵列数，当前为 5 列，与 ui_sensor_page.c 中的小号字库保持一致。
 *
 * 后续调整：
 *     如果后续替换为更宽或更窄的小号字库，需要同步调整本宏和字库中每个字符的 column 数组长度。
 */
#define UI_MAIN_SMALL_FONT_WIDTH             5U

/* UI_MAIN_SMALL_FONT_HEIGHT：Sensor 数据区小号 ASCII 字体点阵高度，单位点阵行。
 *
 * 功能：
 *     控制 UI 内部小号字体每个字符的点阵行数，当前为 7 行，与 ui_sensor_page.c 中的小号字库保持一致。
 *
 * 后续调整：
 *     如果后续替换为更高或更矮的小号字库，需要同步调整本宏和字库位图数据的有效 bit 行数。
 */
#define UI_MAIN_SMALL_FONT_HEIGHT            7U

/* UI_MAIN_SMALL_FONT_CHAR_GAP：Sensor 数据区小号字体字符间距，单位点阵列/行。
 *
 * 功能：
 *     控制小号字体相邻字符之间预留的默认空白间距，当前用于统一派生横向和换行间距。
 *
 * 后续调整：
 *     如果 Sensor 数据文本看起来过密或过宽，可以先调整本宏；如需横向和纵向分别调试，再修改
 *     UI_MAIN_SENSOR_DATA_FONT_SPACE_X / UI_MAIN_SENSOR_DATA_FONT_SPACE_Y。
 */
#define UI_MAIN_SMALL_FONT_CHAR_GAP          1U

/* UI_MAIN_SENSOR_DATA_FONT_SCALE_X：Sensor 页面环境数据小号字体横向放大倍数。
 *
 * 功能：
 *     控制环境数据文本每个点阵像素在 X 方向的放大倍数。当前设为 1，确保 "Pressure: xx.xx hPa" 不换行。
 */
#define UI_MAIN_SENSOR_DATA_FONT_SCALE_X     1U

/* UI_MAIN_SENSOR_DATA_FONT_SCALE_Y：Sensor 页面环境数据小号字体纵向放大倍数。
 *
 * 功能：
 *     控制环境数据文本每个点阵像素在 Y 方向的放大倍数。当前设为 2，提高小号数据字体的可读性。
 */
#define UI_MAIN_SENSOR_DATA_FONT_SCALE_Y     2U

/* UI_MAIN_SENSOR_DATA_FONT_SPACE_X：Sensor 页面环境数据小号字体字符横向间隔，单位点阵列。
 *
 * 功能：
 *     控制环境数据文本相邻字符之间的空白列数量，后续如果文字过密或过宽可调整本宏。
 */
#define UI_MAIN_SENSOR_DATA_FONT_SPACE_X     UI_MAIN_SMALL_FONT_CHAR_GAP

/* UI_MAIN_SENSOR_DATA_FONT_SPACE_Y：Sensor 页面环境数据小号字体换行纵向间隔，单位点阵行。
 *
 * 功能：
 *     预留给后续多行小号文本自动换行使用，当前四行数据通过 UI_MAIN_SENSOR_LINE_GAP 控制行距。
 */
#define UI_MAIN_SENSOR_DATA_FONT_SPACE_Y     UI_MAIN_SMALL_FONT_CHAR_GAP

/* UI_MAIN_SENSOR_CLEAR_X / UI_MAIN_SENSOR_CLEAR_Y：Sensor 页面环境数据局部清除区域左上角坐标。
 *
 * 功能：
 *     每次刷新环境数据前，只从该坐标开始覆盖数据文本区域，避免每秒整屏清屏造成闪烁。
 */
#define UI_MAIN_SENSOR_CLEAR_X               0U
#define UI_MAIN_SENSOR_CLEAR_Y               104U

/* UI_MAIN_SENSOR_CLEAR_W / UI_MAIN_SENSOR_CLEAR_H：Sensor 页面环境数据局部清除区域尺寸。
 *
 * 功能：
 *     控制局部覆盖矩形的宽度和高度，需覆盖四行数据以及 "Sensor data not ready" 提示文字。
 */
#define UI_MAIN_SENSOR_CLEAR_W               LCD_H_RES
#define UI_MAIN_SENSOR_CLEAR_H               132U

/* UI_MAIN_SENSOR_DATA_TEXT_COLOR：Sensor 页面环境数据文字颜色。
 *
 * 功能：
 *     控制真实环境数据和 "Sensor data not ready" 提示文字的前景色。
 */
#define UI_MAIN_SENSOR_DATA_TEXT_COLOR       LCD_COLOR_WHITE

/* UI_MAIN_SENSOR_DATA_BG_COLOR：Sensor 页面环境数据文字背景色。
 *
 * 功能：
 *     控制环境数据局部清除区域和数据文字背景色，默认与页面背景色保持一致。
 */
#define UI_MAIN_SENSOR_DATA_BG_COLOR         UI_MAIN_SCREEN_COLOR

/* UI_MAIN_SCREEN_COLOR：UI 页面背景色。
 *
 * 功能：
 *     HOME 页面和 Sensor 页面清屏时使用该颜色。
 */
#define UI_MAIN_SCREEN_COLOR                 LCD_COLOR_BLACK

/* UI_MAIN_SENSOR_BTN_COLOR：Sensor 入口按键背景色。
 *
 * 功能：
 *     HOME 页面绘制 "Sensor" 入口按键时使用。
 */
#define UI_MAIN_SENSOR_BTN_COLOR             LCD_COLOR_GREEN

/* UI_MAIN_BACK_BTN_COLOR：Back 返回按键背景色。
 *
 * 功能：
 *     Sensor 页面绘制 "Back" 返回按键时使用。
 */
#define UI_MAIN_BACK_BTN_COLOR               LCD_COLOR_BLUE

/* UI_MAIN_TEXT_COLOR：页面普通文字颜色。
 *
 * 功能：
 *     HOME 标题、Sensor 标题等普通文字使用该颜色。
 */
#define UI_MAIN_TEXT_COLOR                   LCD_COLOR_WHITE

/* UI_MAIN_BUTTON_TEXT_COLOR：当前页面按键文字颜色。
 *
 * 功能：
 *     HOME 页面的 Sensor 入口按键和 Sensor 页面的 Back 返回按键使用该颜色。
 */
#define UI_MAIN_BUTTON_TEXT_COLOR            LCD_COLOR_WHITE

/* UI_MAIN_SENSOR_LINE_BUF_SIZE：Sensor 页面单行数据格式化缓存长度，单位 Byte。
 *
 * 功能：
 *     控制 ui_sensor_page_draw_values() 中用于 snprintf() 的临时行缓存大小，当前足够容纳四行数据文本。
 *
 * 后续调整：
 *     如果后续 Sensor 数据行增加更长标签、更多小数位或额外单位，需要先增大本宏，避免文本被截断。
 */
#define UI_MAIN_SENSOR_LINE_BUF_SIZE         32U

/* UI_MAIN_SENSOR_TEMP_FMT：Sensor 页面温度数据行格式字符串。
 *
 * 功能：
 *     控制温度行的标签、数值精度和单位，当前显示为 "Temperature: xx.xx C"。
 *
 * 后续调整：
 *     如果后续需要改成中文标签、摄氏度符号、更多/更少小数位，优先修改本宏。
 */
#define UI_MAIN_SENSOR_TEMP_FMT              "Temperature: %.2f C"

/* UI_MAIN_SENSOR_HUMI_FMT：Sensor 页面湿度数据行格式字符串。
 *
 * 功能：
 *     控制湿度行的标签、数值精度和单位，当前显示为 "Humidity: xx.xx %"。
 *
 * 后续调整：
 *     如果后续需要改湿度标签、显示 RH 单位或调整小数位，优先修改本宏。
 */
#define UI_MAIN_SENSOR_HUMI_FMT              "Humidity: %.2f %%"

/* UI_MAIN_SENSOR_PRESS_FMT：Sensor 页面气压数据行格式字符串。
 *
 * 功能：
 *     控制气压行的标签、数值精度和单位，当前显示为 "Pressure: xx.xx hPa"。
 *
 * 后续调整：
 *     如果后续需要改成 Pa、kPa 或调整显示精度，优先修改本宏。
 */
#define UI_MAIN_SENSOR_PRESS_FMT             "Pressure: %.2f hPa"

/* UI_MAIN_SENSOR_GAS_FMT：Sensor 页面气体电阻数据行格式字符串。
 *
 * 功能：
 *     控制气体电阻行的标签、数值精度和单位，当前显示为 "Gas: xx.xx kOhm"。
 *
 * 后续调整：
 *     如果后续需要改成 Ohm、MOhm 或增加空气质量提示，优先修改本宏。
 */
#define UI_MAIN_SENSOR_GAS_FMT               "Gas: %.2f kOhm"

/* UI_MAIN_SENSOR_NOT_READY_TEXT：Sensor 页面 ENV 数据未准备好时的提示文字。
 *
 * 功能：
 *     env_get_data() 返回错误或缓存数据无效时显示该文字，不再显示旧的 Waiting 占位文字。
 *
 * 后续调整：
 *     如果后续需要改成中文提示、缩短文字避免占宽，或显示更详细的错误状态，优先修改本宏。
 */
#define UI_MAIN_SENSOR_NOT_READY_TEXT        "Sensor data not ready"

/* UI_MAIN_SMALL_FONT_FALLBACK_CHAR：Sensor 数据区小号字体缺字时使用的替代字符。
 *
 * 功能：
 *     当待显示字符没有收录在小号字体字库中时，使用该字符的点阵作为替代，避免访问空指针。
 *
 * 后续调整：
 *     如果后续小号字库不再收录 '?'，或希望缺字显示为空格、方块等效果，需要同步调整本宏和字库内容。
 */
#define UI_MAIN_SMALL_FONT_FALLBACK_CHAR     '?'

/* ui_main_start：启动 HOME/Sensor 页面切换任务。
 *
 * 功能：
 *     1. 绘制 HOME 页面标题和居中 Sensor 入口按键；
 *     2. 创建 UI 触摸任务，处理 HOME 的 Sensor 入口和 Sensor 页面的 Back 返回。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_init());
 *     ESP_ERROR_CHECK(cst816t_init());
 *     ESP_ERROR_CHECK(ui_main_start());
 *
 * 返回：
 *     ESP_OK：启动成功，或 UI 任务已经运行；
 *     ESP_ERR_NO_MEM：FreeRTOS 任务创建失败；
 *     其它值：LCD 绘图接口返回的错误码。
 */
esp_err_t ui_main_start(void);

#endif
