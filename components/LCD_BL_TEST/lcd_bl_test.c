#include "lcd_bl_test.h"

#include <inttypes.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LCD_BL_TEST";

static TaskHandle_t s_lcd_bl_test_task_handle;

/* lcd_bl_test_delay_ms：背光测试任务毫秒延时。
 *
 * 参数：
 *     delay_ms：需要延时的毫秒数。
 *
 * 调用方法：
 *     lcd_bl_test_delay_ms(LCD_BL_TEST_TOGGLE_PERIOD_MS);
 *
 * 说明：
 *     单独封装延时转换，避免较小毫秒数在低 tick 频率下转换为 0 tick。
 */
static void lcd_bl_test_delay_ms(uint32_t delay_ms)
{
    TickType_t ticks = pdMS_TO_TICKS(delay_ms);

    if (ticks == 0)
    {
        ticks = 1;
    }

    vTaskDelay(ticks);
}

/* lcd_bl_test_print_level：打印当前背光电平状态。
 *
 * 参数：
 *     level：GPIO 输出电平，0 表示低电平，非 0 表示高电平。
 *
 * 调用方法：
 *     lcd_bl_test_print_level(1);     // 打印 BL HIGH
 *     lcd_bl_test_print_level(0);     // 打印 BL LOW
 *
 * 说明：
 *     同时使用 printf 和 ESP_LOGI，满足串口直观输出和 ESP-IDF 日志检索需求。
 */
static void lcd_bl_test_print_level(uint32_t level)
{
    if (level != 0)
    {
        printf("BL HIGH\r\n");
        ESP_LOGI(TAG, "BL HIGH");
    }
    else
    {
        printf("BL LOW\r\n");
        ESP_LOGI(TAG, "BL LOW");
    }
}

/* lcd_bl_test_print_no_light_hint：打印背光仍不亮时的硬件检查提示。
 *
 * 调用方法：
 *     lcd_bl_test_print_no_light_hint();
 *
 * 说明：
 *     程序无法自动判断肉眼看到的背光亮灭，所以在启动时打印排查清单。
 */
static void lcd_bl_test_print_no_light_hint(void)
{
    ESP_LOGI(TAG, "如果背光仍不亮，请检查 LCD VCC");
    ESP_LOGI(TAG, "如果背光仍不亮，请检查 LCD GND");
    ESP_LOGI(TAG, "如果背光仍不亮，请检查 FPC 排线方向");
    ESP_LOGI(TAG, "如果背光仍不亮，请检查 BL 是否需要直接接 3.3V");
}

/* lcd_bl_test_task：LCD 背光 GPIO20 翻转测试任务。
 *
 * 参数：
 *     arg：任务参数，当前未使用，保留给后续扩展。
 *
 * 调用方法：
 *     该函数由 lcd_bl_test_start() 内部通过 xTaskCreatePinnedToCore() 创建，
 *     外部模块不需要也不建议直接调用。
 *
 * 功能：
 *     每 1000ms 翻转一次 GPIO20，持续输出 BL HIGH / BL LOW。
 */
static void lcd_bl_test_task(void *arg)
{
    (void)arg;

    uint32_t level = 0;

    while (1)
    {
        level = (level == 0) ? 1U : 0U;

        esp_err_t ret = gpio_set_level(LCD_BL_TEST_GPIO, (uint32_t)level);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "设置 LCD_BL GPIO%" PRIu32 " 电平失败，ret: %d",
                     (uint32_t)LCD_BL_TEST_GPIO,
                     ret);
        }
        else
        {
            lcd_bl_test_print_level(level);
        }

        lcd_bl_test_delay_ms(LCD_BL_TEST_TOGGLE_PERIOD_MS);
    }
}

esp_err_t lcd_bl_test_start(void)
{
    if (s_lcd_bl_test_task_handle != NULL)
    {
        ESP_LOGI(TAG, "LCD 背光测试任务已经启动");
        return ESP_OK;
    }

    /* 只配置 LCD_BL 对应的 GPIO20，不初始化任何 LCD SPI 或 ST7789 驱动。 */
    const gpio_config_t bl_gpio_config = {
        .pin_bit_mask = (1ULL << LCD_BL_TEST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&bl_gpio_config);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "配置 LCD_BL GPIO%" PRIu32 " 为输出失败，ret: %d",
                 (uint32_t)LCD_BL_TEST_GPIO,
                 ret);
        return ESP_ERR_INVALID_STATE;
    }

    /* 先输出低电平作为已知初始状态，随后任务会每 1000ms 翻转一次。 */
    ret = gpio_set_level(LCD_BL_TEST_GPIO, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "设置 LCD_BL GPIO%" PRIu32 " 初始低电平失败，ret: %d",
                 (uint32_t)LCD_BL_TEST_GPIO,
                 ret);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "LCD 背光最小化测试启动，GPIO%" PRIu32 " -> LCD_BL，翻转周期: %" PRIu32 " ms",
             (uint32_t)LCD_BL_TEST_GPIO,
             (uint32_t)LCD_BL_TEST_TOGGLE_PERIOD_MS);
    ESP_LOGI(TAG, "本测试不初始化 SPI/ST7789/DMA/LVGL/framebuffer，也不调用 fill_screen");
    lcd_bl_test_print_no_light_hint();

    BaseType_t task_ret = xTaskCreatePinnedToCore(lcd_bl_test_task,
                                                   LCD_BL_TEST_TASK_NAME,
                                                   LCD_BL_TEST_TASK_STACK_SIZE,
                                                   NULL,
                                                   LCD_BL_TEST_TASK_PRIORITY,
                                                   &s_lcd_bl_test_task_handle,
                                                   LCD_BL_TEST_TASK_CORE_ID);
    if (task_ret != pdPASS)
    {
        s_lcd_bl_test_task_handle = NULL;
        ESP_LOGE(TAG, "创建 LCD 背光测试任务失败");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
