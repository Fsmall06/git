#ifndef __LCD_H
#define __LCD_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lcd_st7789.h"

/* ============================== LCD 可调参数区 ==============================
 *
 * 说明：
 *     1. 本头文件集中保存 LCD 顶层模块后续调试可能修改的参数。
 *     2. Driver 相关硬件参数放在 Driver/lcd_st7789.h，UI 布局参数放在 UI/lcd_ui.h。
 *     3. LCD 顶层只负责模块初始化、可选刷新任务和测试画面，不直接依赖 ENV/BME690/WiFi/ASR 等业务模块。
 *     4. 当前阶段按“纯色 fill_screen 测试”运行，不启用 LVGL、不启用复杂 UI、不启用多任务刷新。
 */

/* LCD_TASK_NAME：LCD Dashboard 刷新任务名称。
 * 功能：
 *     FreeRTOS 创建任务时使用该名称，便于日志和调试工具识别。
 * 调用方法：
 *     lcd_init(NULL);     // 内部会使用 LCD_TASK_NAME 创建刷新任务
 */
#define LCD_TASK_NAME                         "lcd_task"

/* LCD_TASK_STACK_SIZE：LCD 刷新任务栈大小，单位 Byte。
 * 功能：
 *     Dashboard 刷新会进行字符串格式化、framebuffer 绘制和 SPI 刷屏，默认预留 4096 Byte。
 * 调试建议：
 *     如果后续 UI 增加更多控件、图标或状态机，可适当增大该值。
 */
#define LCD_TASK_STACK_SIZE                   4096

/* LCD_TASK_PRIORITY：LCD 刷新任务优先级。
 * 功能：
 *     控制 LCD 刷新任务在 FreeRTOS 中的调度优先级。
 * 调试建议：
 *     环境 Dashboard 1000ms 刷新一次，默认优先级 4 即可，避免抢占传感器和网络任务。
 */
#define LCD_TASK_PRIORITY                     4

/* LCD_TASK_CORE_ID：LCD 刷新任务 CPU 绑定设置。
 * 功能：
 *     ESP32-C5 为单核 RISC-V 芯片，使用 tskNO_AFFINITY 即可；移植到双核芯片时也可保持该值。
 * 调用方法：
 *     lcd_init(NULL);     // 内部 xTaskCreatePinnedToCore() 会使用该宏
 */
#define LCD_TASK_CORE_ID                      tskNO_AFFINITY

/* LCD_REFRESH_PERIOD_MS：Dashboard 页面刷新周期，单位 ms。
 * 功能：
 *     控制 LCD 任务每隔多久重新读取业务数据并刷新屏幕。
 * 当前需求：
 *     环境数据 Dashboard 刷新周期为 1000ms。
 */
#define LCD_REFRESH_PERIOD_MS                 1000

/* LCD_START_TASK_DEFAULT：LCD 默认是否创建自动刷新任务。
 * 功能：
 *     0：默认只做一次纯色 fill_screen 测试，不创建 LCD 刷新任务；
 *     1：默认创建 LCD 自动刷新任务，后续恢复 Dashboard/UI 时再开启。
 * 调用方法：
 *     lcd_get_default_config(&cfg);     // cfg.start_task 会使用该宏作为默认值
 * 当前需求：
 *     为排查 CPU_LOCKUP、I2C timeout 和 partition table 异常，先保持为 0。
 */
#define LCD_START_TASK_DEFAULT                0

/* LCD_ENABLE_DASHBOARD_UI：是否启用 LCD Dashboard UI 绘制。
 * 功能：
 *     0：lcd_refresh_once() 只执行纯色 fill_screen + flush；
 *     1：lcd_refresh_once() 读取数据回调并绘制 Dashboard。
 * 调用方法：
 *     调试稳定后，把该宏改为 1，并确保 components/LCD/CMakeLists.txt 重新加入 UI 目录。
 * 当前需求：
 *     不启用 LVGL、不启用复杂 UI，因此保持为 0。
 */
#define LCD_ENABLE_DASHBOARD_UI               0

/* LCD_FILL_TEST_COLOR：LCD 纯色测试颜色，RGB565 格式。
 * 功能：
 *     lcd_fill_screen_test() 使用该颜色填充整个屏幕，用于验证 SPI、DC、RST、BL 和供电是否稳定。
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_fill_screen_test());
 * 调试建议：
 *     可改为 LCD_COLOR_RED/GREEN/BLUE/BLACK/WHITE，确认颜色通道和反色设置是否正确。
 */
#define LCD_FILL_TEST_COLOR                   LCD_COLOR_BLUE

/* LCD_WAIT_TICK_MIN：毫秒延时转换成 Tick 后的最小 Tick 数。
 * 功能：
 *     防止较小的 ms 值在低 tick 频率下被 pdMS_TO_TICKS() 转成 0，导致任务空转。
 */
#define LCD_WAIT_TICK_MIN                     1

/* LCD_DEFAULT_STATE_TEXT：未接入业务回调或状态未知时显示的默认文本。
 * 功能：
 *     WiFi/ASR 状态还未由外部模块提供时，Dashboard 使用该文本占位。
 */
#define LCD_DEFAULT_STATE_TEXT                "N/A"

/* LCD_DATA_DEFAULT_VALUE：环境数据无效时用于显示的默认数值。
 * 功能：
 *     传感器尚未完成首次采样时，Dashboard 会显示该值，避免使用未初始化数据。
 */
#define LCD_DATA_DEFAULT_VALUE                0.0f

/* ============================== LCD 数据结构定义 ============================== */

typedef struct _lcd_dashboard_data_t
{
    float temperature_c;              /* 温度，单位：摄氏度，用于 Dashboard 第一项显示 */
    float humidity_percent;           /* 相对湿度，单位：%RH，用于 Dashboard 第二项显示 */
    float pressure_hpa;               /* 气压，单位：hPa，用于 Dashboard 第三项显示 */
    uint32_t gas_resistance_ohm;      /* 气体电阻，单位：Ohm，用于 Dashboard 第四项显示 */
    bool env_valid;                   /* 环境数据是否有效；false 时 UI 会显示默认值和等待状态 */
    bool wifi_connected;              /* WiFi 是否已连接；true 显示 ON，false 显示 OFF */
    bool asr_active;                  /* ASR 是否处于可用/激活状态；true 显示 ON，false 显示 OFF */
} lcd_dashboard_data_t;

typedef esp_err_t (*lcd_dashboard_data_cb_t)(lcd_dashboard_data_t *data, void *user_ctx);

typedef struct _lcd_config_t
{
    lcd_st7789_config_t driver_config;        /* ST7789 底层驱动配置；通常使用 lcd_get_default_config() 获取默认值 */
    lcd_dashboard_data_cb_t data_cb;          /* Dashboard 数据回调；可为 NULL，为 NULL 时显示默认占位数据 */
    void *user_ctx;                           /* 传给 data_cb 的用户上下文；LCD 模块不会解析该指针 */
    bool start_task;                          /* 是否启动自动刷新任务；当前纯色测试阶段默认 false，避免多任务刷屏干扰 I2C */
} lcd_config_t;

/* ============================== LCD 对外接口声明 ============================== */

/* lcd_get_default_config：获取 LCD 顶层默认配置。
 *
 * 功能：
 *     1. 填充 ST7789 SPI/GPIO/屏幕尺寸默认参数；
 *     2. 默认不绑定业务数据回调；
 *     3. 默认不启动 LCD 自动刷新任务，只做一次纯色 fill_screen 测试。
 *
 * 参数：
 *     config：输出参数，用于保存默认配置，不能为 NULL。
 *
 * 返回：
 *     ESP_OK：获取成功；
 *     ESP_ERR_INVALID_ARG：config 为 NULL。
 *
 * 调用方法：
 *     lcd_config_t cfg = {0};
 *     ESP_ERROR_CHECK(lcd_get_default_config(&cfg));
 *     cfg.data_cb = app_lcd_get_dashboard_data;
 *     ESP_ERROR_CHECK(lcd_init(&cfg));
 */
esp_err_t lcd_get_default_config(lcd_config_t *config);

/* lcd_init：初始化 LCD 模块并按配置启动 Dashboard。
 *
 * 功能：
 *     1. 初始化 ST7789 SPI Master、GPIO、framebuffer 和芯片寄存器；
 *     2. 当前阶段绘制一次纯色 fill_screen 测试画面；
 *     3. 根据 start_task 决定是否创建刷新任务，默认不创建。
 *
 * 参数：
 *     config：LCD 配置；传 NULL 时使用 lcd_get_default_config() 的默认配置。
 *
 * 返回：
 *     ESP_OK：初始化成功；
 *     其它值：底层 SPI/GPIO/framebuffer/任务创建失败。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_init(NULL));        // 使用默认参数做一次纯色填充测试
 *
 *     lcd_config_t cfg = {0};
 *     lcd_get_default_config(&cfg);
 *     cfg.data_cb = app_lcd_get_dashboard_data;
 *     ESP_ERROR_CHECK(lcd_init(&cfg));
 */
esp_err_t lcd_init(const lcd_config_t *config);

/* lcd_refresh_once：手动刷新一次 Dashboard 页面。
 *
 * 功能：
 *     当前阶段只执行纯色 fill_screen + flush；后续打开 LCD_ENABLE_DASHBOARD_UI 后，
 *     才会读取数据回调并绘制 Dashboard。
 *
 * 返回：
 *     ESP_OK：刷新成功；
 *     ESP_ERR_INVALID_STATE：LCD 尚未初始化；
 *     其它值：绘制或刷屏失败。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_refresh_once());
 *
 * 说明：
 *     当前纯色测试阶段可手动调用本函数重复刷入同一纯色画面。
 */
esp_err_t lcd_refresh_once(void);

/* lcd_fill_screen_test：执行一次 LCD 纯色填充测试。
 *
 * 功能：
 *     1. 使用 LCD_FILL_TEST_COLOR 填充 ST7789 framebuffer；
 *     2. 调用 lcd_st7789_flush() 通过 SPI DMA 发送到屏幕；
 *     3. 不访问 ENV/BME690，不创建 UI，不启动 LVGL。
 *
 * 返回：
 *     ESP_OK：纯色刷屏成功；
 *     ESP_ERR_INVALID_STATE：LCD 或 ST7789 Driver 尚未初始化；
 *     其它值：底层 fill_screen 或 SPI flush 失败。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_init(NULL));             // lcd_init() 内部会先调用本函数
 *     ESP_ERROR_CHECK(lcd_fill_screen_test());     // 需要重复验证时可再次手动调用
 */
esp_err_t lcd_fill_screen_test(void);

/* lcd_set_dashboard_data_callback：运行时更新 Dashboard 数据回调。
 *
 * 功能：
 *     用于 WiFi/ASR 等业务模块启动顺序发生变化时，后续再给 LCD 绑定数据来源。
 *
 * 参数：
 *     data_cb：新的数据回调；可为 NULL，表示恢复默认占位数据。
 *     user_ctx：传给 data_cb 的用户上下文。
 *
 * 返回：
 *     ESP_OK：设置成功；
 *     ESP_ERR_INVALID_STATE：LCD 尚未初始化。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_set_dashboard_data_callback(app_lcd_get_dashboard_data, NULL));
 */
esp_err_t lcd_set_dashboard_data_callback(lcd_dashboard_data_cb_t data_cb, void *user_ctx);

/* lcd_is_initialized：查询 LCD 模块是否已经初始化完成。
 *
 * 返回：
 *     true：lcd_init() 已成功完成；
 *     false：LCD 尚未初始化或初始化失败。
 *
 * 调用方法：
 *     if (lcd_is_initialized()) {
 *         lcd_refresh_once();
 *     }
 */
bool lcd_is_initialized(void);

#endif
