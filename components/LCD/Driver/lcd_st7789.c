#include "lcd_st7789.h"

#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LCD_ST7789";

typedef struct _lcd_st7789_state_t
{
    lcd_st7789_config_t config;          /* 当前 Driver 运行配置，初始化后固定保存 */
    spi_device_handle_t spi;             /* ESP-IDF SPI Master 设备句柄 */
    uint16_t *framebuffer;               /* RGB565 framebuffer，按像素保存，内存需支持 DMA */
    bool bus_initialized;                /* SPI 总线是否已经初始化 */
    bool initialized;                    /* ST7789 Driver 是否已经完成初始化 */
} lcd_st7789_state_t;

typedef struct _lcd_st7789_gpio_item_t
{
    const char *name;                    /* GPIO 功能名称，用于冲突日志和启动分配表 */
    gpio_num_t gpio;                     /* GPIO 编号，可为 GPIO_NUM_NC */
} lcd_st7789_gpio_item_t;

static lcd_st7789_state_t s_lcd;         /* Driver 私有状态，外部只能通过公开接口访问 */

/* lcd_st7789_delay_ms：Driver 内部毫秒延时。
 *
 * 参数：
 *     ms：需要延时的毫秒数。
 *
 * 调用方法：
 *     lcd_st7789_delay_ms(LCD_ST7789_RESET_LOW_MS);
 */
static void lcd_st7789_delay_ms(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);

    if (ticks == 0)
    {
        ticks = LCD_ST7789_WAIT_TICK_MIN;
    }

    vTaskDelay(ticks);
}

/* lcd_st7789_color_to_bus：把 RGB565 颜色转换成 ST7789 SPI 发送字节序。
 *
 * 参数：
 *     color：常规 RGB565 颜色值，例如 LCD_COLOR_WHITE。
 *
 * 返回：
 *     适合直接放入 framebuffer 并通过 SPI DMA 发送的 16bit 数据。
 *
 * 调用方法：
 *     framebuffer[index] = lcd_st7789_color_to_bus(LCD_COLOR_CYAN);
 *
 * 说明：
 *     ESP32-C5 为小端存储，uint16_t 0xF800 在内存中是 00 F8；
 *     ST7789 RAMWR 需要先收高字节 F8 再收低字节 00，因此这里在写入 framebuffer 前交换字节。
 */
static uint16_t lcd_st7789_color_to_bus(uint16_t color)
{
    return (uint16_t)((color << 8) | (color >> 8));
}

/* lcd_st7789_gpio_is_used：判断一个 GPIO 是否需要配置。
 *
 * 参数：
 *     gpio：GPIO 编号，可为 GPIO_NUM_NC。
 *
 * 返回：
 *     true：该 GPIO 有效，需要配置；
 *     false：该 GPIO 未连接，跳过配置。
 *
 * 调用方法：
 *     if (lcd_st7789_gpio_is_used(config->pin_rst)) { ... }
 */
static bool lcd_st7789_gpio_is_used(gpio_num_t gpio)
{
    return gpio != GPIO_NUM_NC;
}

/* lcd_st7789_log_gpio_table：打印 LCD、BME690 I2C 与禁用脚的 GPIO 分配表。
 *
 * 参数：
 *     config：待打印的 LCD Driver 配置，不能为 NULL。
 *
 * 调用方法：
 *     lcd_st7789_log_gpio_table(&local_config);
 *
 * 说明：
 *     该函数只打印分配关系，不修改任何 GPIO 状态；用于确认 LCD SPI、背光与
 *     BME690 I2C、console UART 禁用脚已完全分离。
 */
static void lcd_st7789_log_gpio_table(const lcd_st7789_config_t *config)
{
    if (config == NULL)
    {
        return;
    }

    ESP_LOGI(TAG,
             "GPIO 分配表: BME690_I2C_SDA:%d, BME690_I2C_SCL:%d, LCD_MOSI:%d, LCD_SCLK:%d, LCD_CS:%d, LCD_DC:%d, LCD_RST:%d, LCD_BL:%d",
             (int)LCD_ST7789_RESERVED_I2C_SDA_GPIO,
             (int)LCD_ST7789_RESERVED_I2C_SCL_GPIO,
             (int)config->pin_mosi,
             (int)config->pin_sclk,
             (int)config->pin_cs,
             (int)config->pin_dc,
             (int)config->pin_rst,
             (int)config->pin_bl);
    ESP_LOGI(TAG,
             "LCD 禁用 GPIO: GPIO2, GPIO3, GPIO11, GPIO12, GPIO13；当前 LCD 推荐接线为 MOSI=6 SCLK=7 CS=10 DC=18 RST=19 BL=20");
}

/* lcd_st7789_check_reserved_gpio：检查 LCD GPIO 是否占用了 I2C 保留脚或禁用脚。
 *
 * 参数：
 *     gpio：待检查的 LCD GPIO，可为 GPIO_NUM_NC。
 *     name：GPIO 功能名称，例如 "MOSI"、"SCLK"、"BL"。
 *
 * 返回：
 *     ESP_OK：该 GPIO 未触碰保留/禁用范围；
 *     ESP_ERR_INVALID_ARG：该 GPIO 与 I2C GPIO2/GPIO3、console UART GPIO11/GPIO12
 *                          或板级禁用 GPIO13 冲突。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_st7789_check_reserved_gpio(config->pin_mosi, "MOSI"));
 */
static esp_err_t lcd_st7789_check_reserved_gpio(gpio_num_t gpio, const char *name)
{
    if (!lcd_st7789_gpio_is_used(gpio))
    {
        return ESP_OK;
    }

    if (gpio == LCD_ST7789_RESERVED_I2C_SDA_GPIO)
    {
        ESP_LOGE(TAG, "LCD %s GPIO%d 与 BME690 I2C SDA GPIO%d 冲突",
                 name,
                 (int)gpio,
                 (int)LCD_ST7789_RESERVED_I2C_SDA_GPIO);
        return ESP_ERR_INVALID_ARG;
    }

    if (gpio == LCD_ST7789_RESERVED_I2C_SCL_GPIO)
    {
        ESP_LOGE(TAG, "LCD %s GPIO%d 与 BME690 I2C SCL GPIO%d 冲突",
                 name,
                 (int)gpio,
                 (int)LCD_ST7789_RESERVED_I2C_SCL_GPIO);
        return ESP_ERR_INVALID_ARG;
    }

    if ((gpio == LCD_ST7789_AVOID_GPIO_11) ||
        (gpio == LCD_ST7789_AVOID_GPIO_12) ||
        (gpio == LCD_ST7789_AVOID_GPIO_13))
    {
        ESP_LOGE(TAG, "LCD %s GPIO%d 命中禁用脚 GPIO11/GPIO12/GPIO13，请换到 ESP32-C5 安全 GPIO",
                 name,
                 (int)gpio);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/* lcd_st7789_check_lcd_gpio_unique：检查 LCD 模块内部 GPIO 是否重复使用。
 *
 * 参数：
 *     gpios：LCD GPIO 分配数组，不能为 NULL。
 *     gpio_count：数组元素数量。
 *
 * 返回：
 *     ESP_OK：LCD 所有已连接 GPIO 互不重复；
 *     ESP_ERR_INVALID_ARG：发现两个 LCD 信号复用了同一个 GPIO。
 *
 * 调用方法：
 *     lcd_st7789_check_lcd_gpio_unique(gpios, gpio_count);
 */
static esp_err_t lcd_st7789_check_lcd_gpio_unique(const lcd_st7789_gpio_item_t *gpios, size_t gpio_count)
{
    if (gpios == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < gpio_count; i++)
    {
        if (!lcd_st7789_gpio_is_used(gpios[i].gpio))
        {
            continue;
        }

        for (size_t j = i + 1; j < gpio_count; j++)
        {
            if (!lcd_st7789_gpio_is_used(gpios[j].gpio))
            {
                continue;
            }

            if (gpios[i].gpio == gpios[j].gpio)
            {
                ESP_LOGE(TAG, "LCD GPIO 复用冲突: %s 和 %s 同时使用 GPIO%d",
                         gpios[i].name,
                         gpios[j].name,
                         (int)gpios[i].gpio);
                return ESP_ERR_INVALID_ARG;
            }
        }
    }

    return ESP_OK;
}

/* lcd_st7789_check_gpio_conflict：集中检查 LCD GPIO 与 I2C GPIO、禁用 GPIO、自身复用是否冲突。
 *
 * 参数：
 *     config：待检查的 LCD Driver 配置，不能为 NULL。
 *
 * 返回：
 *     ESP_OK：GPIO 分配安全；
 *     ESP_ERR_INVALID_ARG：存在 GPIO2/3、GPIO11/12/13 或 LCD 自身复用冲突。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_st7789_check_gpio_conflict(&cfg));
 */
static esp_err_t lcd_st7789_check_gpio_conflict(const lcd_st7789_config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const lcd_st7789_gpio_item_t gpios[] = {
        {"MOSI", config->pin_mosi},
        {"SCLK", config->pin_sclk},
        {"CS", config->pin_cs},
        {"DC", config->pin_dc},
        {"RST", config->pin_rst},
        {"BL", config->pin_bl},
    };
    const size_t gpio_count = sizeof(gpios) / sizeof(gpios[0]);

    for (size_t i = 0; i < gpio_count; i++)
    {
        esp_err_t ret = lcd_st7789_check_reserved_gpio(gpios[i].gpio, gpios[i].name);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    return lcd_st7789_check_lcd_gpio_unique(gpios, gpio_count);
}

/* lcd_st7789_get_spi_bus_flags：生成 SPI 总线初始化 flags。
 *
 * 返回：
 *     SPI Master 总线能力和路由标志，包含 MASTER/MOSI/SCLK，并可按宏强制走 GPIO matrix。
 *
 * 调用方法：
 *     spi_bus_config_t bus_cfg = { .flags = lcd_st7789_get_spi_bus_flags() };
 */
static uint32_t lcd_st7789_get_spi_bus_flags(void)
{
    uint32_t flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_MOSI | SPICOMMON_BUSFLAG_SCLK;

#if LCD_ST7789_SPI_FORCE_GPIO_MATRIX
    flags |= SPICOMMON_BUSFLAG_GPIO_PINS;
#endif

    return flags;
}

/* lcd_st7789_check_output_gpio：检查输出 GPIO 是否有效。
 *
 * 参数：
 *     gpio：需要检查的 GPIO。
 *     name：用于日志打印的 GPIO 名称。
 *
 * 返回：
 *     ESP_OK：GPIO 合法；
 *     ESP_ERR_INVALID_ARG：GPIO 不支持输出。
 *
 * 调用方法：
 *     ESP_RETURN_ON_ERROR(lcd_st7789_check_output_gpio(cfg->pin_dc, "DC"), TAG, "DC 引脚非法");
 */
static esp_err_t lcd_st7789_check_output_gpio(gpio_num_t gpio, const char *name)
{
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio))
    {
        ESP_LOGE(TAG, "%s GPIO 无效或不支持输出，gpio: %d", name, (int)gpio);
        return ESP_ERR_INVALID_ARG;
    }

    return ESP_OK;
}

/* lcd_st7789_config_output_gpio：把指定 GPIO 配置为推挽输出。
 *
 * 参数：
 *     gpio：需要配置的 GPIO。
 *     default_level：配置完成后的默认输出电平。
 *     name：用于日志打印的 GPIO 名称。
 *
 * 返回：
 *     ESP_OK：配置成功；
 *     其它值：GPIO 驱动返回的错误。
 *
 * 调用方法：
 *     lcd_st7789_config_output_gpio(config->pin_dc, 0, "DC");
 */
static esp_err_t lcd_st7789_config_output_gpio(gpio_num_t gpio, uint32_t default_level, const char *name)
{
    esp_err_t ret = lcd_st7789_check_output_gpio(gpio, name);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = gpio_reset_pin(gpio);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "%s GPIO reset 失败，gpio: %d, ret: %d", name, (int)gpio, ret);
        return ret;
    }

    ret = gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "%s GPIO 设置输出失败，gpio: %d, ret: %d", name, (int)gpio, ret);
        return ret;
    }

    ret = gpio_set_level(gpio, default_level);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "%s GPIO 设置默认电平失败，gpio: %d, ret: %d", name, (int)gpio, ret);
    }

    return ret;
}

/* lcd_st7789_check_config：检查 Driver 配置是否满足基本要求。
 *
 * 参数：
 *     config：待检查配置，不能为 NULL。
 *
 * 返回：
 *     ESP_OK：配置合法；
 *     ESP_ERR_INVALID_ARG：存在非法参数。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_st7789_check_config(&cfg));
 */
static esp_err_t lcd_st7789_check_config(const lcd_st7789_config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if ((config->width == 0) || (config->height == 0))
    {
        ESP_LOGE(TAG, "LCD 宽高不能为 0，width: %u, height: %u",
                 (unsigned int)config->width,
                 (unsigned int)config->height);
        return ESP_ERR_INVALID_ARG;
    }

    lcd_st7789_log_gpio_table(config);

    esp_err_t ret = lcd_st7789_check_gpio_conflict(config);
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (!GPIO_IS_VALID_OUTPUT_GPIO(config->pin_mosi) || !GPIO_IS_VALID_OUTPUT_GPIO(config->pin_sclk))
    {
        ESP_LOGE(TAG, "SPI MOSI/SCLK GPIO 无效，MOSI: %d, SCLK: %d",
                 (int)config->pin_mosi,
                 (int)config->pin_sclk);
        return ESP_ERR_INVALID_ARG;
    }

    ret = lcd_st7789_check_output_gpio(config->pin_dc, "DC");
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (lcd_st7789_gpio_is_used(config->pin_cs))
    {
        ret = lcd_st7789_check_output_gpio(config->pin_cs, "CS");
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    if (lcd_st7789_gpio_is_used(config->pin_rst))
    {
        ret = lcd_st7789_check_output_gpio(config->pin_rst, "RST");
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    if (lcd_st7789_gpio_is_used(config->pin_bl))
    {
        ret = lcd_st7789_check_output_gpio(config->pin_bl, "BL");
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    return ESP_OK;
}

/* lcd_st7789_set_dc：设置 D/C 引脚电平。
 *
 * 参数：
 *     level：0 表示命令，1 表示数据。
 *
 * 返回：
 *     ESP_OK：设置成功；
 *     其它值：GPIO 驱动错误。
 *
 * 调用方法：
 *     lcd_st7789_set_dc(0);     // 下一笔 SPI 为命令
 */
static esp_err_t lcd_st7789_set_dc(uint32_t level)
{
    return gpio_set_level(s_lcd.config.pin_dc, level);
}

/* lcd_st7789_spi_transmit：阻塞发送一笔 SPI 数据。
 *
 * 参数：
 *     data：待发送数据指针，不能为 NULL。
 *     len：待发送长度，单位 Byte，不能为 0。
 *
 * 返回：
 *     ESP_OK：发送成功；
 *     ESP_ERR_INVALID_ARG：参数错误；
 *     其它值：SPI 驱动错误。
 *
 * 调用方法：
 *     uint8_t cmd = LCD_ST7789_CMD_SWRESET;
 *     lcd_st7789_spi_transmit(&cmd, 1);
 */
static esp_err_t lcd_st7789_spi_transmit(const void *data, size_t len)
{
    if ((data == NULL) || (len == 0))
    {
        return ESP_ERR_INVALID_ARG;
    }

    spi_transaction_t trans = {
        .length = len * 8U,
        .tx_buffer = data,
    };

    return spi_device_transmit(s_lcd.spi, &trans);
}

/* lcd_st7789_write_command：向 ST7789 发送单字节命令。
 *
 * 参数：
 *     cmd：ST7789 命令字节。
 *
 * 返回：
 *     ESP_OK：发送成功；
 *     其它值：GPIO 或 SPI 错误。
 *
 * 调用方法：
 *     lcd_st7789_write_command(LCD_ST7789_CMD_SLPOUT);
 */
static esp_err_t lcd_st7789_write_command(uint8_t cmd)
{
    esp_err_t ret = lcd_st7789_set_dc(0);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return lcd_st7789_spi_transmit(&cmd, sizeof(cmd));
}

/* lcd_st7789_write_data：向 ST7789 发送数据参数。
 *
 * 参数：
 *     data：待发送数据指针，不能为 NULL。
 *     len：待发送长度，单位 Byte。
 *
 * 返回：
 *     ESP_OK：发送成功；
 *     其它值：GPIO 或 SPI 错误。
 *
 * 调用方法：
 *     uint8_t mode = LCD_ST7789_COLOR_MODE_VALUE;
 *     lcd_st7789_write_data(&mode, 1);
 */
static esp_err_t lcd_st7789_write_data(const void *data, size_t len)
{
    esp_err_t ret = lcd_st7789_set_dc(1);
    if (ret != ESP_OK)
    {
        return ret;
    }

    return lcd_st7789_spi_transmit(data, len);
}

/* lcd_st7789_write_command_data：发送命令和可选参数。
 *
 * 参数：
 *     cmd：ST7789 命令。
 *     data：命令参数，可为 NULL。
 *     len：命令参数长度，data 为 NULL 时必须为 0。
 *
 * 返回：
 *     ESP_OK：发送成功；
 *     其它值：发送失败。
 *
 * 调用方法：
 *     uint8_t color_mode = 0x55;
 *     lcd_st7789_write_command_data(LCD_ST7789_CMD_COLMOD, &color_mode, 1);
 */
static esp_err_t lcd_st7789_write_command_data(uint8_t cmd, const void *data, size_t len)
{
    esp_err_t ret = lcd_st7789_write_command(cmd);
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (len > 0)
    {
        return lcd_st7789_write_data(data, len);
    }

    return ESP_OK;
}

/* lcd_st7789_hardware_reset：执行 ST7789 硬件复位。
 *
 * 返回：
 *     ESP_OK：复位完成；
 *     其它值：GPIO 设置失败。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_st7789_hardware_reset());
 */
static esp_err_t lcd_st7789_hardware_reset(void)
{
    if (!lcd_st7789_gpio_is_used(s_lcd.config.pin_rst))
    {
        ESP_LOGI(TAG, "RST 未连接，跳过硬件复位");
        return ESP_OK;
    }

    esp_err_t ret = gpio_set_level(s_lcd.config.pin_rst, 0);
    if (ret != ESP_OK)
    {
        return ret;
    }

    lcd_st7789_delay_ms(LCD_ST7789_RESET_LOW_MS);

    ret = gpio_set_level(s_lcd.config.pin_rst, 1);
    if (ret != ESP_OK)
    {
        return ret;
    }

    lcd_st7789_delay_ms(LCD_ST7789_RESET_HIGH_MS);
    return ESP_OK;
}

/* lcd_st7789_set_address_window：设置 ST7789 后续写显存的窗口范围。
 *
 * 参数：
 *     x0/y0：窗口左上角坐标。
 *     x1/y1：窗口右下角坐标。
 *
 * 返回：
 *     ESP_OK：设置成功；
 *     其它值：SPI 发送失败。
 *
 * 调用方法：
 *     lcd_st7789_set_address_window(0, 0, width - 1, height - 1);
 */
static esp_err_t lcd_st7789_set_address_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    const uint16_t start_x = (uint16_t)(x0 + s_lcd.config.x_offset);
    const uint16_t end_x = (uint16_t)(x1 + s_lcd.config.x_offset);
    const uint16_t start_y = (uint16_t)(y0 + s_lcd.config.y_offset);
    const uint16_t end_y = (uint16_t)(y1 + s_lcd.config.y_offset);

    uint8_t column_data[4] = {
        (uint8_t)(start_x >> 8),
        (uint8_t)(start_x & 0xFF),
        (uint8_t)(end_x >> 8),
        (uint8_t)(end_x & 0xFF),
    };
    uint8_t row_data[4] = {
        (uint8_t)(start_y >> 8),
        (uint8_t)(start_y & 0xFF),
        (uint8_t)(end_y >> 8),
        (uint8_t)(end_y & 0xFF),
    };

    esp_err_t ret = lcd_st7789_write_command_data(LCD_ST7789_CMD_CASET, column_data, sizeof(column_data));
    if (ret != ESP_OK)
    {
        return ret;
    }

    return lcd_st7789_write_command_data(LCD_ST7789_CMD_RASET, row_data, sizeof(row_data));
}

/* lcd_st7789_init_gpio：初始化 ST7789 控制 GPIO。
 *
 * 返回：
 *     ESP_OK：GPIO 初始化成功；
 *     其它值：GPIO 配置失败。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_st7789_init_gpio());
 */
static esp_err_t lcd_st7789_init_gpio(void)
{
    esp_err_t ret = lcd_st7789_config_output_gpio(s_lcd.config.pin_dc, 0, "DC");
    if (ret != ESP_OK)
    {
        return ret;
    }

    if (lcd_st7789_gpio_is_used(s_lcd.config.pin_rst))
    {
        ret = lcd_st7789_config_output_gpio(s_lcd.config.pin_rst, 1, "RST");
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    if (lcd_st7789_gpio_is_used(s_lcd.config.pin_bl))
    {
        const uint32_t bl_off_level = (s_lcd.config.bl_on_level == 0) ? 1U : 0U;
        ret = lcd_st7789_config_output_gpio(s_lcd.config.pin_bl, bl_off_level, "BL");
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    return ESP_OK;
}

/* lcd_st7789_init_spi：初始化 SPI Master 总线并添加 ST7789 设备。
 *
 * 返回：
 *     ESP_OK：SPI 初始化成功；
 *     其它值：SPI 驱动错误。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_st7789_init_spi());
 */
static esp_err_t lcd_st7789_init_spi(void)
{
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = s_lcd.config.pin_mosi,
        .miso_io_num = GPIO_NUM_NC,
        .sclk_io_num = s_lcd.config.pin_sclk,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .data4_io_num = GPIO_NUM_NC,
        .data5_io_num = GPIO_NUM_NC,
        .data6_io_num = GPIO_NUM_NC,
        .data7_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_ST7789_MAX_DMA_CHUNK,
        .flags = lcd_st7789_get_spi_bus_flags(),
    };

    esp_err_t ret = spi_bus_initialize(s_lcd.config.host, &bus_cfg, s_lcd.config.dma_chan);
    if ((ret != ESP_OK) && (ret != ESP_ERR_INVALID_STATE))
    {
        ESP_LOGE(TAG, "SPI 总线初始化失败，ret: %d", ret);
        return ret;
    }

    s_lcd.bus_initialized = true;
    if (ret == ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(TAG, "SPI 总线已初始化，继续添加 ST7789 设备");
    }

    spi_device_interface_config_t dev_cfg = {
        .mode = LCD_ST7789_SPI_MODE,
        .clock_speed_hz = s_lcd.config.spi_clock_hz,
        .spics_io_num = s_lcd.config.pin_cs,
        .queue_size = LCD_ST7789_SPI_QUEUE_SIZE,
    };

    ret = spi_bus_add_device(s_lcd.config.host, &dev_cfg, &s_lcd.spi);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "添加 ST7789 SPI 设备失败，ret: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG,
             "SPI 初始化完成，host: %d, SCLK: %d, MOSI: %d, CS: %d, clock: %d Hz, GPIO matrix: %d, DMA: auto",
             (int)s_lcd.config.host,
             (int)s_lcd.config.pin_sclk,
             (int)s_lcd.config.pin_mosi,
             (int)s_lcd.config.pin_cs,
             s_lcd.config.spi_clock_hz,
             LCD_ST7789_SPI_FORCE_GPIO_MATRIX);

    return ESP_OK;
}

/* lcd_st7789_alloc_framebuffer：申请 DMA-capable framebuffer。
 *
 * 返回：
 *     ESP_OK：申请成功；
 *     ESP_ERR_NO_MEM：内存不足。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_st7789_alloc_framebuffer());
 */
static esp_err_t lcd_st7789_alloc_framebuffer(void)
{
    if (s_lcd.framebuffer != NULL)
    {
        return ESP_OK;
    }

    const size_t fb_size = (size_t)s_lcd.config.width * (size_t)s_lcd.config.height * LCD_ST7789_BYTES_PER_PIXEL;
    s_lcd.framebuffer = (uint16_t *)heap_caps_malloc(fb_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (s_lcd.framebuffer == NULL)
    {
        ESP_LOGE(TAG, "申请 framebuffer 失败，size: %u Byte", (unsigned int)fb_size);
        return ESP_ERR_NO_MEM;
    }

    memset(s_lcd.framebuffer, 0, fb_size);
    ESP_LOGI(TAG, "framebuffer 申请成功，size: %u Byte", (unsigned int)fb_size);
    return ESP_OK;
}

/* lcd_st7789_init_panel：发送 ST7789 初始化命令序列。
 *
 * 返回：
 *     ESP_OK：初始化命令发送成功；
 *     其它值：SPI/GPIO 错误。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_st7789_init_panel());
 */
static esp_err_t lcd_st7789_init_panel(void)
{
    esp_err_t ret = lcd_st7789_hardware_reset();
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_st7789_write_command(LCD_ST7789_CMD_SWRESET);
    if (ret != ESP_OK)
    {
        return ret;
    }
    lcd_st7789_delay_ms(LCD_ST7789_RESET_HIGH_MS);

    ret = lcd_st7789_write_command(LCD_ST7789_CMD_SLPOUT);
    if (ret != ESP_OK)
    {
        return ret;
    }
    lcd_st7789_delay_ms(LCD_ST7789_SLEEP_OUT_DELAY_MS);

    uint8_t color_mode = LCD_ST7789_COLOR_MODE_VALUE;
    ret = lcd_st7789_write_command_data(LCD_ST7789_CMD_COLMOD, &color_mode, sizeof(color_mode));
    if (ret != ESP_OK)
    {
        return ret;
    }

    uint8_t madctl = LCD_ST7789_MADCTL_VALUE;
    ret = lcd_st7789_write_command_data(LCD_ST7789_CMD_MADCTL, &madctl, sizeof(madctl));
    if (ret != ESP_OK)
    {
        return ret;
    }

#if LCD_ST7789_INVERSION_ENABLE
    ret = lcd_st7789_write_command(LCD_ST7789_CMD_INVON);
#else
    ret = lcd_st7789_write_command(LCD_ST7789_CMD_INVOFF);
#endif
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_st7789_write_command(LCD_ST7789_CMD_NORON);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_st7789_write_command(LCD_ST7789_CMD_DISPON);
    if (ret != ESP_OK)
    {
        return ret;
    }
    lcd_st7789_delay_ms(LCD_ST7789_DISPLAY_ON_DELAY_MS);

    if (lcd_st7789_gpio_is_used(s_lcd.config.pin_bl))
    {
        ret = gpio_set_level(s_lcd.config.pin_bl, (uint32_t)s_lcd.config.bl_on_level);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }

    return ESP_OK;
}

esp_err_t lcd_st7789_get_default_config(lcd_st7789_config_t *config)
{
    if (config == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    config->host = LCD_ST7789_HOST;
    config->dma_chan = LCD_ST7789_SPI_DMA_CH;
    config->pin_mosi = LCD_ST7789_PIN_MOSI;
    config->pin_sclk = LCD_ST7789_PIN_SCLK;
    config->pin_cs = LCD_ST7789_PIN_CS;
    config->pin_dc = LCD_ST7789_PIN_DC;
    config->pin_rst = LCD_ST7789_PIN_RST;
    config->pin_bl = LCD_ST7789_PIN_BL;
    config->bl_on_level = LCD_ST7789_BL_ON_LEVEL;
    config->width = LCD_ST7789_WIDTH;
    config->height = LCD_ST7789_HEIGHT;
    config->x_offset = LCD_ST7789_X_OFFSET;
    config->y_offset = LCD_ST7789_Y_OFFSET;
    config->spi_clock_hz = LCD_ST7789_SPI_CLOCK_HZ;

    return ESP_OK;
}

esp_err_t lcd_st7789_init(const lcd_st7789_config_t *config)
{
    if (s_lcd.initialized)
    {
        ESP_LOGI(TAG, "ST7789 Driver 已初始化");
        return ESP_OK;
    }

    lcd_st7789_config_t local_config = {0};
    esp_err_t ret;

    if (config == NULL)
    {
        ret = lcd_st7789_get_default_config(&local_config);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }
    else
    {
        local_config = *config;
    }

    ret = lcd_st7789_check_config(&local_config);
    if (ret != ESP_OK)
    {
        return ret;
    }

    memset(&s_lcd, 0, sizeof(s_lcd));
    s_lcd.config = local_config;

    ret = lcd_st7789_init_gpio();
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_st7789_init_spi();
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_st7789_alloc_framebuffer();
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_st7789_init_panel();
    if (ret != ESP_OK)
    {
        return ret;
    }

    s_lcd.initialized = true;

    ret = lcd_st7789_fill_screen(LCD_COLOR_BLACK);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_st7789_flush();
    if (ret != ESP_OK)
    {
        return ret;
    }

    ESP_LOGI(TAG, "ST7789 初始化完成，width: %u, height: %u",
             (unsigned int)s_lcd.config.width,
             (unsigned int)s_lcd.config.height);

    return ESP_OK;
}

esp_err_t lcd_st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (!s_lcd.initialized || (s_lcd.framebuffer == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    if ((x >= s_lcd.config.width) || (y >= s_lcd.config.height))
    {
        return ESP_ERR_INVALID_ARG;
    }

    s_lcd.framebuffer[(size_t)y * (size_t)s_lcd.config.width + (size_t)x] = lcd_st7789_color_to_bus(color);
    return ESP_OK;
}

esp_err_t lcd_st7789_fill_screen(uint16_t color)
{
    if (!s_lcd.initialized || (s_lcd.framebuffer == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    const size_t pixel_count = (size_t)s_lcd.config.width * (size_t)s_lcd.config.height;
    for (size_t i = 0; i < pixel_count; i++)
    {
        s_lcd.framebuffer[i] = lcd_st7789_color_to_bus(color);
    }

    return ESP_OK;
}

esp_err_t lcd_st7789_draw_char(uint16_t x,
                               uint16_t y,
                               char ch,
                               const lcd_font_t *font,
                               uint16_t color,
                               uint16_t bg_color)
{
    if (!s_lcd.initialized || (s_lcd.framebuffer == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    if ((font == NULL) ||
        (font->width == 0) ||
        (font->height == 0) ||
        (font->base_width == 0) ||
        (font->base_height == 0) ||
        (font->scale_x == 0) ||
        (font->scale_y == 0) ||
        (font->get_glyph == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if ((x >= s_lcd.config.width) || (y >= s_lcd.config.height))
    {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t *glyph = font->get_glyph(ch);
    if (glyph == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint8_t row = 0; row < font->height; row++)
    {
        const uint16_t draw_y = (uint16_t)(y + row);
        if (draw_y >= s_lcd.config.height)
        {
            break;
        }

        for (uint8_t col = 0; col < font->width; col++)
        {
            const uint16_t draw_x = (uint16_t)(x + col);
            if (draw_x >= s_lcd.config.width)
            {
                break;
            }

            bool pixel_on = false;
            if ((col >= font->offset_x) && (row >= font->offset_y))
            {
                const uint8_t scaled_col = (uint8_t)((col - font->offset_x) / font->scale_x);
                const uint8_t scaled_row = (uint8_t)((row - font->offset_y) / font->scale_y);

                if ((scaled_col < font->base_width) && (scaled_row < font->base_height))
                {
                    const uint8_t row_bits = glyph[scaled_row];
                    pixel_on = (row_bits & (uint8_t)(1U << (font->base_width - 1U - scaled_col))) != 0;
                }
            }

            s_lcd.framebuffer[(size_t)draw_y * (size_t)s_lcd.config.width + (size_t)draw_x] =
                lcd_st7789_color_to_bus(pixel_on ? color : bg_color);
        }
    }

    return ESP_OK;
}

esp_err_t lcd_st7789_draw_string(uint16_t x,
                                 uint16_t y,
                                 const char *str,
                                 const lcd_font_t *font,
                                 uint16_t color,
                                 uint16_t bg_color)
{
    if (!s_lcd.initialized || (s_lcd.framebuffer == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    if ((str == NULL) || (font == NULL))
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t cursor_x = x;
    for (size_t i = 0; str[i] != '\0'; i++)
    {
        if ((cursor_x + font->width) > s_lcd.config.width)
        {
            break;
        }

        esp_err_t ret = lcd_st7789_draw_char(cursor_x, y, str[i], font, color, bg_color);
        if (ret != ESP_OK)
        {
            return ret;
        }

        cursor_x = (uint16_t)(cursor_x + font->width);
    }

    return ESP_OK;
}

esp_err_t lcd_st7789_draw_number(uint16_t x,
                                 uint16_t y,
                                 float value,
                                 uint8_t decimals,
                                 const lcd_font_t *font,
                                 uint16_t color,
                                 uint16_t bg_color)
{
    if (decimals > 3)
    {
        decimals = 3;
    }

    char number_buf[24] = {0};
    int written = snprintf(number_buf, sizeof(number_buf), "%.*f", (int)decimals, (double)value);
    if ((written < 0) || ((size_t)written >= sizeof(number_buf)))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    return lcd_st7789_draw_string(x, y, number_buf, font, color, bg_color);
}

esp_err_t lcd_st7789_flush(void)
{
    if (!s_lcd.initialized || (s_lcd.framebuffer == NULL) || (s_lcd.spi == NULL))
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = lcd_st7789_set_address_window(0,
                                                   0,
                                                   (uint16_t)(s_lcd.config.width - 1U),
                                                   (uint16_t)(s_lcd.config.height - 1U));
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_st7789_write_command(LCD_ST7789_CMD_RAMWR);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = lcd_st7789_set_dc(1);
    if (ret != ESP_OK)
    {
        return ret;
    }

    const size_t total_bytes = (size_t)s_lcd.config.width * (size_t)s_lcd.config.height * LCD_ST7789_BYTES_PER_PIXEL;
    const uint8_t *tx_ptr = (const uint8_t *)s_lcd.framebuffer;
    size_t bytes_sent = 0;

    while (bytes_sent < total_bytes)
    {
        size_t chunk_len = total_bytes - bytes_sent;
        if (chunk_len > LCD_ST7789_MAX_DMA_CHUNK)
        {
            chunk_len = LCD_ST7789_MAX_DMA_CHUNK;
        }

        ret = lcd_st7789_spi_transmit(&tx_ptr[bytes_sent], chunk_len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "SPI DMA 刷屏失败，sent: %u, chunk: %u, ret: %d",
                     (unsigned int)bytes_sent,
                     (unsigned int)chunk_len,
                     ret);
            return ret;
        }

        bytes_sent += chunk_len;

#if LCD_ST7789_DMA_YIELD_ENABLE
        taskYIELD();
#endif
    }

    return ESP_OK;
}

uint16_t lcd_st7789_get_width(void)
{
    if (!s_lcd.initialized)
    {
        return LCD_ST7789_WIDTH;
    }

    return s_lcd.config.width;
}

uint16_t lcd_st7789_get_height(void)
{
    if (!s_lcd.initialized)
    {
        return LCD_ST7789_HEIGHT;
    }

    return s_lcd.config.height;
}

bool lcd_st7789_is_initialized(void)
{
    return s_lcd.initialized;
}
