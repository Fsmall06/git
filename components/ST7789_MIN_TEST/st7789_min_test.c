#include "st7789_min_test.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ST7789_MIN";

/* s_lcd_spi：ST7789 SPI 设备句柄。
 * 说明：
 *     st7789_min_init() 添加 SPI 设备成功后写入，lcd_write_cmd()/lcd_write_data() 会使用它发送数据。
 */
static spi_device_handle_t s_lcd_spi = NULL;

/* s_line_buffer：小块 DMA 行缓冲。
 * 说明：
 *     本验证程序不使用 framebuffer，只申请 ST7789_MIN_TRANSFER_ROWS 行大小的 DMA 内存反复发送。
 */
static uint16_t *s_line_buffer = NULL;

/* s_lcd_ready：LCD 初始化完成标志。
 * 说明：
 *     lcd_fill_color() 会检查该标志，避免 ST7789 未初始化时误刷屏。
 */
static bool s_lcd_ready = false;

static esp_err_t st7789_min_trace_step_error(esp_err_t ret, const char *step)
{
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "%s ESP_ERROR_CHECK ret=%d", step, ret);
    }

    return ret;
}

#define ST7789_MIN_RETURN_ON_STEP_ERROR(expr, step)       \
    do                                                    \
    {                                                     \
        esp_err_t step_ret = st7789_min_trace_step_error((expr), (step)); \
        if (step_ret != ESP_OK)                           \
        {                                                 \
            return step_ret;                              \
        }                                                 \
    } while (0)

/* st7789_min_delay_ms：毫秒延时封装。
 *
 * 参数：
 *     ms：需要延时的毫秒数。
 *
 * 调用方法：
 *     st7789_min_delay_ms(ST7789_MIN_RESET_LOW_MS);
 *
 * 说明：
 *     使用 vTaskDelay() 让出 CPU，避免长时间忙等导致 CPU_LOCKUP。
 */
static void st7789_min_delay_ms(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);

    if (ticks == 0)
    {
        ticks = 1;
    }

    vTaskDelay(ticks);
}

/* st7789_min_rgb565_to_bus：把 RGB565 颜色转换为 ST7789 需要的高字节先发格式。
 *
 * 参数：
 *     color：RGB565 颜色，例如 0xF800 表示红色。
 *
 * 返回：
 *     适合直接放入 uint16_t DMA 发送缓冲的字节序。
 *
 * 调用方法：
 *     uint16_t bus_color = st7789_min_rgb565_to_bus(ST7789_MIN_COLOR_RED);
 *
 * 说明：
 *     ESP32-C5 内存为小端序，uint16_t 0xF800 在内存中是 00 F8；
 *     ST7789 RGB565 像素需要先收到高字节 F8，再收到低字节 00，所以这里预先交换字节。
 */
static uint16_t st7789_min_rgb565_to_bus(uint16_t color)
{
    return (uint16_t)((color << 8) | (color >> 8));
}

/* st7789_min_spi_tx：阻塞发送一笔 SPI 数据。
 *
 * 参数：
 *     data：待发送数据地址。
 *     len：待发送字节数。
 *
 * 返回：
 *     ESP_OK：发送成功。
 *     ESP_ERR_INVALID_STATE：SPI 设备尚未添加。
 *     ESP_ERR_INVALID_ARG：参数无效。
 *     其它值：SPI 驱动返回的错误。
 *
 * 调用方法：
 *     uint8_t cmd = ST7789_MIN_CMD_SWRESET;
 *     ESP_ERROR_CHECK(st7789_min_spi_tx(&cmd, sizeof(cmd)));
 */
static esp_err_t st7789_min_spi_tx(const void *data, size_t len)
{
    if (s_lcd_spi == NULL)
    {
        ESP_LOGE(TAG, "SPI 设备尚未初始化，无法发送 LCD 数据");
        return ESP_ERR_INVALID_STATE;
    }

    if ((data == NULL) || (len == 0))
    {
        ESP_LOGE(TAG, "SPI 发送参数无效，data=%p, len=%u", data, (unsigned int)len);
        return ESP_ERR_INVALID_ARG;
    }

    spi_transaction_t trans = {
        .length = len * 8U,
        .tx_buffer = data,
    };

    return spi_device_transmit(s_lcd_spi, &trans);
}

/* st7789_min_gpio_config_output：配置一个 GPIO 为普通输出。
 *
 * 参数：
 *     gpio：需要配置的 GPIO 编号。
 *     level：配置完成后的默认输出电平。
 *     name：日志中显示的引脚名称，例如 "DC"。
 *
 * 返回：
 *     ESP_OK：配置成功。
 *     其它值：GPIO 驱动返回的错误。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(st7789_min_gpio_config_output(ST7789_MIN_PIN_BL, 1, "BL"));
 */
static esp_err_t st7789_min_gpio_config_output(gpio_num_t gpio, uint32_t level, const char *name)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio))
    {
        ESP_LOGE(TAG, "%s GPIO%d 不是有效输出引脚", name, (int)gpio);
        return ESP_ERR_INVALID_ARG;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "配置 %s GPIO%d 为输出失败，ret=%d", name, (int)gpio, ret);
        return ret;
    }

    ret = gpio_set_level(gpio, level);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "设置 %s GPIO%d 初始电平失败，ret=%d", name, (int)gpio, ret);
        return ret;
    }

    return ESP_OK;
}

/* st7789_min_init_gpio：初始化背光、复位和 D/C 控制 GPIO。
 *
 * 返回：
 *     ESP_OK：GPIO 初始化成功。
 *     其它值：GPIO 配置失败。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(st7789_min_init_gpio());
 *
 * 说明：
 *     这里会立即把 GPIO20 背光拉到点亮电平，满足“背光常亮”的验证要求。
 */
static esp_err_t st7789_min_init_gpio(void)
{
    ST7789_MIN_RETURN_ON_STEP_ERROR(st7789_min_gpio_config_output(ST7789_MIN_PIN_BL, ST7789_MIN_BL_ON_LEVEL, "BL"), "BL ON");
    ESP_LOGI(TAG, "BL ON");
    ST7789_MIN_RETURN_ON_STEP_ERROR(st7789_min_gpio_config_output(ST7789_MIN_PIN_DC, 0, "DC"), "DC INIT");
    ST7789_MIN_RETURN_ON_STEP_ERROR(st7789_min_gpio_config_output(ST7789_MIN_PIN_RST, 1, "RST"), "RST INIT");
    return ESP_OK;
}

/* st7789_min_hard_reset：执行 ST7789 硬件复位。
 *
 * 返回：
 *     ESP_OK：复位成功。
 *     其它值：GPIO 设置失败。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(st7789_min_hard_reset());
 *
 * 时序：
 *     RST=0，延时 100ms；
 *     RST=1，延时 120ms。
 */
static esp_err_t st7789_min_hard_reset(void)
{
    ST7789_MIN_RETURN_ON_STEP_ERROR(gpio_set_level(ST7789_MIN_PIN_RST, 0), "RST LOW");
    ESP_LOGI(TAG, "RST LOW");
    st7789_min_delay_ms(ST7789_MIN_RESET_LOW_MS);

    ST7789_MIN_RETURN_ON_STEP_ERROR(gpio_set_level(ST7789_MIN_PIN_RST, 1), "RST HIGH");
    ESP_LOGI(TAG, "RST HIGH");
    st7789_min_delay_ms(ST7789_MIN_RESET_HIGH_MS);

    return ESP_OK;
}

/* st7789_min_init_spi：初始化 SPI 总线并添加 ST7789 设备。
 *
 * 返回：
 *     ESP_OK：SPI 初始化成功。
 *     其它值：SPI 总线或设备添加失败。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(st7789_min_init_spi());
 *
 * 配置：
 *     Mode 0、40MHz、Half Duplex、DMA 自动。
 */
static esp_err_t st7789_min_init_spi(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = ST7789_MIN_PIN_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = ST7789_MIN_PIN_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
        .max_transfer_sz = ST7789_MIN_MAX_TRANSFER_BYTES,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_MOSI | SPICOMMON_BUSFLAG_SCLK,
    };

    esp_err_t ret = spi_bus_initialize(ST7789_MIN_HOST, &bus_cfg, ST7789_MIN_DMA_CH);
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE))
    {
        ESP_LOGE(TAG, "SPI INIT ESP_ERROR_CHECK ret=%d", ret);
        return ret;
    }

    if (ret == ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(TAG, "SPI 总线已初始化，继续添加 ST7789 设备");
    }

    spi_device_interface_config_t dev_cfg = {
        .mode = ST7789_MIN_SPI_MODE,
        .clock_speed_hz = ST7789_MIN_SPI_CLOCK_HZ,
        .spics_io_num = ST7789_MIN_PIN_CS,
        .queue_size = ST7789_MIN_SPI_QUEUE_SIZE,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };

    ret = spi_bus_add_device(ST7789_MIN_HOST, &dev_cfg, &s_lcd_spi);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "SPI INIT ESP_ERROR_CHECK ret=%d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "SPI INIT OK");
    return ESP_OK;
}

/* st7789_min_alloc_line_buffer：申请纯色刷屏用的小块 DMA 缓冲。
 *
 * 返回：
 *     ESP_OK：申请成功。
 *     ESP_ERR_NO_MEM：DMA 内存不足。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(st7789_min_alloc_line_buffer());
 *
 * 说明：
 *     只申请 8 行像素缓冲，不申请整屏 framebuffer。
 */
static esp_err_t st7789_min_alloc_line_buffer(void)
{
    if (s_line_buffer != NULL)
    {
        return ESP_OK;
    }

    s_line_buffer = (uint16_t *)heap_caps_malloc(ST7789_MIN_MAX_TRANSFER_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_line_buffer == NULL)
    {
        ESP_LOGE(TAG, "申请 LCD DMA 行缓冲失败，size=%u Byte", (unsigned int)ST7789_MIN_MAX_TRANSFER_BYTES);
        return ESP_ERR_NO_MEM;
    }

    memset(s_line_buffer, 0, ST7789_MIN_MAX_TRANSFER_BYTES);
    ESP_LOGI(TAG, "LCD DMA 行缓冲申请成功，size=%u Byte，不使用整屏 framebuffer", (unsigned int)ST7789_MIN_MAX_TRANSFER_BYTES);
    return ESP_OK;
}

/* st7789_min_set_address_window：设置 ST7789 写显存窗口。
 *
 * 参数：
 *     x0/y0：窗口左上角坐标。
 *     x1/y1：窗口右下角坐标。
 *
 * 返回：
 *     ESP_OK：设置成功。
 *     其它值：命令或数据发送失败。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(st7789_min_set_address_window(0, 0, 239, 239));
 */
static esp_err_t st7789_min_set_address_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    const uint16_t sx = (uint16_t)(x0 + ST7789_MIN_X_OFFSET);
    const uint16_t ex = (uint16_t)(x1 + ST7789_MIN_X_OFFSET);
    const uint16_t sy = (uint16_t)(y0 + ST7789_MIN_Y_OFFSET);
    const uint16_t ey = (uint16_t)(y1 + ST7789_MIN_Y_OFFSET);

    uint8_t col_data[4] = {
        (uint8_t)(sx >> 8),
        (uint8_t)(sx & 0xFF),
        (uint8_t)(ex >> 8),
        (uint8_t)(ex & 0xFF),
    };
    uint8_t row_data[4] = {
        (uint8_t)(sy >> 8),
        (uint8_t)(sy & 0xFF),
        (uint8_t)(ey >> 8),
        (uint8_t)(ey & 0xFF),
    };

    ESP_RETURN_ON_ERROR(lcd_write_cmd(ST7789_MIN_CMD_CASET), TAG, "发送 CASET 命令失败");
    ESP_RETURN_ON_ERROR(lcd_write_data(col_data, sizeof(col_data)), TAG, "发送 CASET 参数失败");
    ESP_RETURN_ON_ERROR(lcd_write_cmd(ST7789_MIN_CMD_RASET), TAG, "发送 RASET 命令失败");
    ESP_RETURN_ON_ERROR(lcd_write_data(row_data, sizeof(row_data)), TAG, "发送 RASET 参数失败");

    return ESP_OK;
}

esp_err_t lcd_write_cmd(uint8_t cmd)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(ST7789_MIN_PIN_DC, 0), TAG, "设置 DC=0 失败");
    return st7789_min_spi_tx(&cmd, sizeof(cmd));
}

esp_err_t lcd_write_data(const void *data, size_t len)
{
    ESP_RETURN_ON_ERROR(gpio_set_level(ST7789_MIN_PIN_DC, 1), TAG, "设置 DC=1 失败");
    return st7789_min_spi_tx(data, len);
}

esp_err_t st7789_min_init(void)
{
    if (s_lcd_ready)
    {
        ESP_LOGI(TAG, "ST7789 最小验证已经初始化完成");
        return ESP_OK;
    }

    ST7789_MIN_RETURN_ON_STEP_ERROR(st7789_min_init_gpio(), "GPIO INIT");
    ST7789_MIN_RETURN_ON_STEP_ERROR(st7789_min_hard_reset(), "RESET");
    ST7789_MIN_RETURN_ON_STEP_ERROR(st7789_min_init_spi(), "SPI INIT");

    ST7789_MIN_RETURN_ON_STEP_ERROR(lcd_write_cmd(ST7789_MIN_CMD_SWRESET), "CMD 0x01");
    ESP_LOGI(TAG, "CMD 0x01 OK");
    st7789_min_delay_ms(ST7789_MIN_SWRESET_DELAY_MS);

    ST7789_MIN_RETURN_ON_STEP_ERROR(lcd_write_cmd(ST7789_MIN_CMD_SLPOUT), "CMD 0x11");
    ESP_LOGI(TAG, "CMD 0x11 OK");
    st7789_min_delay_ms(ST7789_MIN_SLPOUT_DELAY_MS);

    uint8_t color_mode = ST7789_MIN_COLMOD_RGB565;
    ST7789_MIN_RETURN_ON_STEP_ERROR(lcd_write_cmd(ST7789_MIN_CMD_COLMOD), "CMD 0x3A");
    ST7789_MIN_RETURN_ON_STEP_ERROR(lcd_write_data(&color_mode, sizeof(color_mode)), "CMD 0x3A");
    ESP_LOGI(TAG, "CMD 0x3A OK");

    uint8_t madctl = ST7789_MIN_MADCTL_VALUE;
    ST7789_MIN_RETURN_ON_STEP_ERROR(lcd_write_cmd(ST7789_MIN_CMD_MADCTL), "CMD 0x36");
    ST7789_MIN_RETURN_ON_STEP_ERROR(lcd_write_data(&madctl, sizeof(madctl)), "CMD 0x36");
    ESP_LOGI(TAG, "CMD 0x36 OK");

    ST7789_MIN_RETURN_ON_STEP_ERROR(lcd_write_cmd(ST7789_MIN_CMD_DISPON), "CMD 0x29");
    ESP_LOGI(TAG, "CMD 0x29 OK");
    st7789_min_delay_ms(ST7789_MIN_DISPON_DELAY_MS);

    s_lcd_ready = true;

    return ESP_OK;
}

esp_err_t lcd_fill_color(uint16_t color)
{
    if (!s_lcd_ready)
    {
        ESP_LOGE(TAG, "LCD 尚未初始化，不能执行纯色填充");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_line_buffer == NULL)
    {
        ESP_RETURN_ON_ERROR(st7789_min_alloc_line_buffer(), TAG, "LCD DMA 行缓冲申请失败");
    }

    ESP_LOGI(TAG, "开始全屏纯色填充：RGB565=0x%04X，尺寸=%dx%d",
             color,
             ST7789_MIN_LCD_WIDTH,
             ST7789_MIN_LCD_HEIGHT);

    ESP_RETURN_ON_ERROR(st7789_min_set_address_window(0,
                                                       0,
                                                       (uint16_t)(ST7789_MIN_LCD_WIDTH - 1),
                                                       (uint16_t)(ST7789_MIN_LCD_HEIGHT - 1)),
                        TAG,
                        "设置全屏地址窗口失败");
    ESP_RETURN_ON_ERROR(lcd_write_cmd(ST7789_MIN_CMD_RAMWR), TAG, "发送 RAMWR 命令失败");
    ESP_RETURN_ON_ERROR(gpio_set_level(ST7789_MIN_PIN_DC, 1), TAG, "设置 DC=1 进入像素数据阶段失败");

    const uint16_t bus_color = st7789_min_rgb565_to_bus(color);
    const size_t pixels_per_chunk = (size_t)ST7789_MIN_LCD_WIDTH * (size_t)ST7789_MIN_TRANSFER_ROWS;
    for (size_t i = 0; i < pixels_per_chunk; i++)
    {
        s_line_buffer[i] = bus_color;
    }

    size_t rows_sent = 0;
    while (rows_sent < ST7789_MIN_LCD_HEIGHT)
    {
        size_t rows_this_time = ST7789_MIN_LCD_HEIGHT - rows_sent;
        if (rows_this_time > ST7789_MIN_TRANSFER_ROWS)
        {
            rows_this_time = ST7789_MIN_TRANSFER_ROWS;
        }

        const size_t bytes_this_time = (size_t)ST7789_MIN_LCD_WIDTH * rows_this_time * sizeof(uint16_t);
        esp_err_t ret = st7789_min_spi_tx(s_line_buffer, bytes_this_time);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "SPI DMA 发送纯色数据失败，已发送行=%u，本次行=%u，ret=%d",
                     (unsigned int)rows_sent,
                     (unsigned int)rows_this_time,
                     ret);
            return ret;
        }

        rows_sent += rows_this_time;
        taskYIELD();
    }

    ESP_LOGI(TAG, "全屏纯色填充完成：RGB565=0x%04X", color);
    return ESP_OK;
}
