#ifndef LCD_BL_TEST_H
#define LCD_BL_TEST_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/gpio_types.h"

/* ============================== LCD 背光测试可调参数 ==============================
 *
 * 说明：
 *     1. 本头文件只保存 LCD 背光 GPIO 最小化测试需要后续调试的参数；
 *     2. 本模块只依赖 GPIO、FreeRTOS 和日志，不依赖 LCD/SPI/ST7789/LVGL；
 *     3. 后续如果更换背光引脚、翻转周期或任务参数，只需要修改这里的宏定义。
 */

/* LCD_BL_TEST_GPIO：LCD 背光控制脚。
 *
 * 功能：
 *     指定当前要测试的背光 GPIO，本项目硬件连接为 GPIO20 -> LCD_BL。
 *
 * 调用方法：
 *     lcd_bl_test_start();     // 内部会把 LCD_BL_TEST_GPIO 配置为输出并循环翻转
 */
#define LCD_BL_TEST_GPIO                 GPIO_NUM_20

/* LCD_BL_TEST_TOGGLE_PERIOD_MS：背光电平翻转周期，单位 ms。
 *
 * 功能：
 *     控制 GPIO20 每隔多少毫秒翻转一次，高电平和低电平各保持该时长。
 *
 * 调试建议：
 *     默认 1000ms，便于用肉眼观察背光是否闪烁，也便于用万用表测量电平变化。
 */
#define LCD_BL_TEST_TOGGLE_PERIOD_MS     1000U

/* LCD_BL_TEST_TASK_NAME：背光测试任务名称。
 *
 * 功能：
 *     FreeRTOS 创建任务时使用该名称，方便在日志或调试工具中识别任务。
 */
#define LCD_BL_TEST_TASK_NAME            "lcd_bl_test"

/* LCD_BL_TEST_TASK_STACK_SIZE：背光测试任务栈大小，单位 Byte。
 *
 * 功能：
 *     本任务只执行 GPIO 翻转和日志打印，2048 Byte 足够使用。
 *
 * 调试建议：
 *     如果后续增加更多串口输出或检测逻辑，可以适当增大该值。
 */
#define LCD_BL_TEST_TASK_STACK_SIZE      2048U

/* LCD_BL_TEST_TASK_PRIORITY：背光测试任务优先级。
 *
 * 功能：
 *     控制背光测试任务在 FreeRTOS 中的调度优先级。
 *
 * 调试建议：
 *     当前只做 1000ms 翻转测试，优先级 4 即可。
 */
#define LCD_BL_TEST_TASK_PRIORITY        4U

/* LCD_BL_TEST_TASK_CORE_ID：背光测试任务 CPU 绑定设置。
 *
 * 功能：
 *     ESP32-C5 是单核芯片，使用 tskNO_AFFINITY 即可；后续移植到双核芯片也可保持该值。
 */
#define LCD_BL_TEST_TASK_CORE_ID         tskNO_AFFINITY

/* lcd_bl_test_start：启动 LCD 背光 GPIO20 最小化测试。
 *
 * 功能：
 *     1. 配置 GPIO20 为普通输出；
 *     2. 创建独立 FreeRTOS 任务；
 *     3. 任务中每 1000ms 翻转一次 GPIO20；
 *     4. 串口和 ESP_LOGI 分别打印 BL HIGH、BL LOW；
 *     5. 不初始化 SPI/ST7789/DMA/LVGL/framebuffer，不调用 fill_screen。
 *
 * 返回：
 *     ESP_OK：背光测试任务启动成功，或已经启动；
 *     ESP_ERR_INVALID_STATE：GPIO20 配置失败；
 *     ESP_ERR_NO_MEM：任务创建失败。
 *
 * 调用方法：
 *     void app_main(void)
 *     {
 *         ESP_ERROR_CHECK(lcd_bl_test_start());
 *     }
 */
esp_err_t lcd_bl_test_start(void);

#endif
