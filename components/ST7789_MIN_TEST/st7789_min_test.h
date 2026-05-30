#ifndef ST7789_MIN_TEST_H
#define ST7789_MIN_TEST_H

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

/* ============================== ST7789 最小验证可调参数 ==============================
 *
 * 说明：
 *     1. 本头文件集中保存后续调试可能要修改的 LCD 参数，方便后期直接改宏定义。
 *     2. 本模块只依赖 ESP-IDF GPIO、SPI、FreeRTOS 延时、heap 和日志，不依赖 BME690。
 *     3. 本模块不使用 LVGL，不使用 framebuffer，不创建多任务，便于后续移植到其它工程。
 */

/* ST7789_MIN_HOST：LCD 使用的 SPI 主机控制器。
 * 作用：
 *     ESP32-C5 上通用外设 SPI 使用 SPI2_HOST，SPI0/SPI1 通常留给内部 flash。
 * 调用方法：
 *     st7789_min_init(); 内部会把该宏传给 spi_bus_initialize() 和 spi_bus_add_device()。
 */
#define ST7789_MIN_HOST                         SPI2_HOST

/* ST7789_MIN_DMA_CH：SPI DMA 通道选择。
 * 作用：
 *     SPI_DMA_CH_AUTO 表示由 ESP-IDF 自动分配 DMA 通道，满足“DMA 自动”的验证要求。
 * 调用方法：
 *     spi_bus_initialize(ST7789_MIN_HOST, &bus_cfg, ST7789_MIN_DMA_CH);
 */
#define ST7789_MIN_DMA_CH                       SPI_DMA_CH_AUTO

/* ST7789_MIN_PIN_MOSI：ST7789 SPI MOSI/SDA/DIN 引脚。 */
#define ST7789_MIN_PIN_MOSI                     GPIO_NUM_6

/* ST7789_MIN_PIN_SCLK：ST7789 SPI SCLK/SCL/CLK 引脚。 */
#define ST7789_MIN_PIN_SCLK                     GPIO_NUM_7

/* ST7789_MIN_PIN_CS：ST7789 SPI CS 片选引脚。 */
#define ST7789_MIN_PIN_CS                       GPIO_NUM_10

/* ST7789_MIN_PIN_DC：ST7789 D/C 命令数据选择引脚，0 表示命令，1 表示数据。 */
#define ST7789_MIN_PIN_DC                       GPIO_NUM_18

/* ST7789_MIN_PIN_RST：ST7789 硬件复位引脚。 */
#define ST7789_MIN_PIN_RST                      GPIO_NUM_19

/* ST7789_MIN_PIN_BL：LCD 背光控制引脚，本验证程序初始化后保持常亮。 */
#define ST7789_MIN_PIN_BL                       GPIO_NUM_20

/* ST7789_MIN_BL_ON_LEVEL：背光点亮电平。
 * 调试建议：
 *     常见 ST7789 模块为高电平点亮；若你的背光是低电平点亮，把该宏改为 0。
 */
#define ST7789_MIN_BL_ON_LEVEL                  1

/* ST7789_MIN_LCD_WIDTH：LCD 逻辑宽度，单位像素。 */
#define ST7789_MIN_LCD_WIDTH                    240

/* ST7789_MIN_LCD_HEIGHT：LCD 逻辑高度，单位像素。 */
#define ST7789_MIN_LCD_HEIGHT                   240

/* ST7789_MIN_X_OFFSET：列地址偏移。
 * 调试建议：
 *     若纯色画面整体左右错位，可以调整该值。
 */
#define ST7789_MIN_X_OFFSET                     0

/* ST7789_MIN_Y_OFFSET：行地址偏移。
 * 调试建议：
 *     240x240 ST7789 模块有时需要 0 或 80，若上下错位可优先调这个宏。
 */
#define ST7789_MIN_Y_OFFSET                     0

/* ST7789_MIN_SPI_CLOCK_HZ：SPI 时钟频率，单位 Hz。
 * 作用：
 *     当前按要求固定为 40MHz；若飞线较长导致花屏，可临时降低到 20MHz 排查。
 */
#define ST7789_MIN_SPI_CLOCK_HZ                 (40 * 1000 * 1000)

/* ST7789_MIN_SPI_MODE：SPI 模式。
 * 作用：
 *     ST7789 常用 Mode 0，即 CPOL=0、CPHA=0。
 */
#define ST7789_MIN_SPI_MODE                     0

/* ST7789_MIN_SPI_QUEUE_SIZE：SPI 事务队列深度。
 * 作用：
 *     本验证程序使用阻塞式 spi_device_transmit()，队列深度 1 足够。
 */
#define ST7789_MIN_SPI_QUEUE_SIZE               1

/* ST7789_MIN_TRANSFER_ROWS：单次 SPI DMA 发送的像素行数。
 * 作用：
 *     不使用整屏 framebuffer，只申请一个小行缓冲反复发送，降低内存占用并避免 CPU_LOCKUP。
 */
#define ST7789_MIN_TRANSFER_ROWS                8

/* ST7789_MIN_MAX_TRANSFER_BYTES：单次 SPI DMA 最大传输字节数。
 * 作用：
 *     240 像素 * 2 字节 * 8 行 = 3840 字节，适合作为小块 DMA 发送长度。
 */
#define ST7789_MIN_MAX_TRANSFER_BYTES           (ST7789_MIN_LCD_WIDTH * 2 * ST7789_MIN_TRANSFER_ROWS)

/* ST7789_MIN_RESET_LOW_MS：硬复位 RST=0 保持时间，单位 ms。 */
#define ST7789_MIN_RESET_LOW_MS                 100

/* ST7789_MIN_RESET_HIGH_MS：硬复位 RST=1 后等待时间，单位 ms。 */
#define ST7789_MIN_RESET_HIGH_MS                120

/* ST7789_MIN_SWRESET_DELAY_MS：发送 0x01 Software Reset 后等待时间，单位 ms。 */
#define ST7789_MIN_SWRESET_DELAY_MS             150

/* ST7789_MIN_SLPOUT_DELAY_MS：发送 0x11 Sleep Out 后等待时间，单位 ms。 */
#define ST7789_MIN_SLPOUT_DELAY_MS              150

/* ST7789_MIN_DISPON_DELAY_MS：发送 0x29 Display ON 后等待时间，单位 ms。 */
#define ST7789_MIN_DISPON_DELAY_MS              50

/* ST7789_MIN_COLOR_HOLD_MS：app_main 中每种纯色保持时间，单位 ms。 */
#define ST7789_MIN_COLOR_HOLD_MS                3000

/* ST7789_MIN_CMD_*：ST7789 最小验证使用的命令字。 */
#define ST7789_MIN_CMD_SWRESET                  0x01
#define ST7789_MIN_CMD_SLPOUT                   0x11
#define ST7789_MIN_CMD_DISPON                   0x29
#define ST7789_MIN_CMD_CASET                    0x2A
#define ST7789_MIN_CMD_RASET                    0x2B
#define ST7789_MIN_CMD_RAMWR                    0x2C
#define ST7789_MIN_CMD_MADCTL                   0x36
#define ST7789_MIN_CMD_COLMOD                   0x3A

/* ST7789_MIN_COLMOD_RGB565：像素格式寄存器参数，0x55 表示 RGB565。 */
#define ST7789_MIN_COLMOD_RGB565                0x55

/* ST7789_MIN_MADCTL_VALUE：屏幕扫描方向和 RGB/BGR 顺序控制值。 */
#define ST7789_MIN_MADCTL_VALUE                 0x00

/* RGB565 常用纯色，供 app_main 直接调用 lcd_fill_color() 验证显示。 */
#define ST7789_MIN_COLOR_RED                    0xF800
#define ST7789_MIN_COLOR_GREEN                  0x07E0
#define ST7789_MIN_COLOR_BLUE                   0x001F
#define ST7789_MIN_COLOR_WHITE                  0xFFFF
#define ST7789_MIN_COLOR_BLACK                  0x0000

/* st7789_min_init：初始化 GPIO、背光、硬复位、SPI 和 ST7789 最小命令序列。
 *
 * 返回：
 *     ESP_OK：初始化成功。
 *     其它值：GPIO、SPI 或 ST7789 命令发送失败。
 *
 * 调用方法：
 *     void app_main(void)
 *     {
 *         ESP_ERROR_CHECK(st7789_min_init());
 *     }
 */
esp_err_t st7789_min_init(void);

/* lcd_write_cmd：向 ST7789 写入 1 字节命令。
 *
 * 参数：
 *     cmd：ST7789 命令字，例如 ST7789_MIN_CMD_SLPOUT。
 *
 * 返回：
 *     ESP_OK：发送成功。
 *     其它值：DC GPIO 或 SPI 发送失败。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_write_cmd(ST7789_MIN_CMD_DISPON));
 */
esp_err_t lcd_write_cmd(uint8_t cmd);

/* lcd_write_data：向 ST7789 写入数据参数或像素数据。
 *
 * 参数：
 *     data：待发送数据地址，不能为 NULL。
 *     len：待发送数据长度，单位 Byte，不能为 0。
 *
 * 返回：
 *     ESP_OK：发送成功。
 *     ESP_ERR_INVALID_ARG：参数为空或长度为 0。
 *     其它值：DC GPIO 或 SPI 发送失败。
 *
 * 调用方法：
 *     uint8_t pixel_format = ST7789_MIN_COLMOD_RGB565;
 *     ESP_ERROR_CHECK(lcd_write_data(&pixel_format, 1));
 */
esp_err_t lcd_write_data(const void *data, size_t len);

/* lcd_fill_color：用指定 RGB565 颜色填充整个屏幕。
 *
 * 参数：
 *     color：RGB565 颜色值，例如 ST7789_MIN_COLOR_RED。
 *
 * 返回：
 *     ESP_OK：全屏纯色发送成功。
 *     其它值：地址窗口设置、RAMWR 命令或 SPI DMA 发送失败。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_fill_color(ST7789_MIN_COLOR_RED));
 */
esp_err_t lcd_fill_color(uint16_t color);

#endif
