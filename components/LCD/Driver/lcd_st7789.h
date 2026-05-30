#ifndef __LCD_ST7789_H
#define __LCD_ST7789_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "hal/spi_types.h"

/* ============================== ST7789 可调参数区 ==============================
 *
 * 说明：
 *     1. 本头文件集中保存 LCD Driver 后续调试可能修改的参数。
 *     2. LCD Driver 只依赖 ESP-IDF SPI Master/GPIO，不依赖 UI、ENV、BME690 等业务模块。
 *     3. 若硬件接线或屏幕方向变化，优先修改本区域宏定义。
 */

/* LCD_ST7789_HOST：ST7789 使用的 SPI 控制器。
 * 功能：
 *     ESP32-C5 通用外设 SPI 为 SPI2_HOST，SPI0/1 通常用于内部 flash，不用于 LCD。
 * 调用方法：
 *     lcd_st7789_config_t cfg = { .host = LCD_ST7789_HOST };
 */
#define LCD_ST7789_HOST                         SPI2_HOST

/* LCD_ST7789_SPI_DMA_CH：SPI DMA 通道选择。
 * 功能：
 *     SPI_DMA_CH_AUTO 表示由 ESP-IDF 自动分配 DMA 通道，满足“SPI DMA 发送”的需求。
 * 调用方法：
 *     spi_bus_initialize(LCD_ST7789_HOST, &bus_cfg, LCD_ST7789_SPI_DMA_CH);
 */
#define LCD_ST7789_SPI_DMA_CH                   SPI_DMA_CH_AUTO

/* LCD_ST7789_PIN_MOSI：LCD SPI MOSI 引脚。
 * 功能：
 *     ST7789 的 SDA/DIN/MOSI 需要连接到该 GPIO。
 * 调试说明：
 *     当前按 ESP32-C5 安全分配使用 GPIO6，避免占用 I2C 的 GPIO2/GPIO3、
 *     console UART 的 GPIO11/GPIO12，以及板级禁用的 GPIO13。
 * 调用方法：
 *     lcd_st7789_get_default_config(&cfg);     // cfg.pin_mosi 会被填充为 LCD_ST7789_PIN_MOSI
 */
#define LCD_ST7789_PIN_MOSI                     GPIO_NUM_6

/* LCD_ST7789_PIN_SCLK：LCD SPI SCLK 引脚。
 * 功能：
 *     ST7789 的 SCL/CLK 需要连接到该 GPIO。
 * 调试说明：
 *     当前按 ESP32-C5 安全分配使用 GPIO7，不与 I2C SDA/SCL 复用。
 */
#define LCD_ST7789_PIN_SCLK                     GPIO_NUM_7

/* LCD_ST7789_PIN_CS：LCD SPI CS 片选引脚。
 * 功能：
 *     使用硬件 CS 管脚控制 ST7789 片选；若屏幕 CS 固定接地，可改为 GPIO_NUM_NC。
 */
#define LCD_ST7789_PIN_CS                       GPIO_NUM_10

/* LCD_ST7789_PIN_DC：LCD D/C 数据命令选择引脚。
 * 功能：
 *     0 表示当前 SPI 数据为命令，1 表示当前 SPI 数据为像素/参数数据。
 * 调试说明：
 *     当前使用 GPIO18，避开 console UART 占用的 GPIO11/GPIO12，避免启动日志、
 *     partition table 校验和 LCD 命令/数据选择信号互相干扰。
 */
#define LCD_ST7789_PIN_DC                       GPIO_NUM_18

/* LCD_ST7789_PIN_RST：LCD 复位引脚。
 * 功能：
 *     用于硬件复位 ST7789；若屏幕 RST 已接到系统复位，可改为 GPIO_NUM_NC。
 * 调试说明：
 *     当前使用 GPIO19，避免复用 I2C GPIO2/GPIO3 和 console UART GPIO11/GPIO12。
 */
#define LCD_ST7789_PIN_RST                      GPIO_NUM_19

/* LCD_ST7789_PIN_BL：LCD 背光控制引脚。
 * 功能：
 *     初始化完成后拉到 LCD_ST7789_BL_ON_LEVEL；若背光常亮或由其它电路控制，可改为 GPIO_NUM_NC。
 * 调试说明：
 *     当前使用 GPIO20，背光控制与 SPI/I2C/console UART 信号完全独立。
 */
#define LCD_ST7789_PIN_BL                       GPIO_NUM_20

/* LCD_ST7789_BL_ON_LEVEL：背光点亮电平。
 * 功能：
 *     多数 ST7789 模块背光高电平点亮；若你的模块低电平点亮，改为 0。
 */
#define LCD_ST7789_BL_ON_LEVEL                  1

/* LCD_ST7789_RESERVED_I2C_SDA_GPIO：I2C SDA 保留 GPIO。
 * 功能：
 *     LCD Driver 初始化时会检查所有 LCD GPIO，禁止占用该引脚，避免 BME690 I2C 写寄存器 timeout。
 * 调用方法：
 *     修改 I2C 接线时，先同步修改 BSP/IIC/iic.h，再同步修改本宏以保持 LCD 冲突检查准确。
 * 说明：
 *     本工程要求保持 BME690 I2C 不变，因此 GPIO2 只保留给 BME690 SDA，LCD 不使用。
 *     LCD 模块不直接 include BSP/IIC/iic.h，是为了保持 LCD Driver 独立、方便单独移植。
 */
#define LCD_ST7789_RESERVED_I2C_SDA_GPIO        GPIO_NUM_2

/* LCD_ST7789_RESERVED_I2C_SCL_GPIO：I2C SCL 保留 GPIO。
 * 功能：
 *     LCD Driver 初始化时禁止任何 LCD SPI、复位或背光信号复用该引脚。
 * 调用方法：
 *     lcd_st7789_init(NULL);     // 内部会自动校验 LCD GPIO 是否碰到本宏。
 * 说明：
 *     本工程要求保持 BME690 I2C 不变，因此 GPIO3 只保留给 BME690 SCL，LCD 不使用。
 */
#define LCD_ST7789_RESERVED_I2C_SCL_GPIO        GPIO_NUM_3

/* LCD_ST7789_AVOID_GPIO_11：LCD 禁用 GPIO11。
 * 功能：
 *     ESP32-C5 当前日志提示 GPIO11 被 console UART 占用，LCD 不能再接到该脚。
 * 调用方法：
 *     lcd_st7789_init(NULL);     // 初始化阶段会自动检查是否误用了 GPIO11。
 */
#define LCD_ST7789_AVOID_GPIO_11                GPIO_NUM_11

/* LCD_ST7789_AVOID_GPIO_12：LCD 禁用 GPIO12。
 * 功能：
 *     ESP32-C5 当前日志提示 GPIO12 被 console UART 占用，LCD 不能再接到该脚。
 */
#define LCD_ST7789_AVOID_GPIO_12                GPIO_NUM_12

/* LCD_ST7789_AVOID_GPIO_13：LCD 禁用 GPIO13。
 * 功能：
 *     当前板级约束要求 LCD 不使用 GPIO13，后续改线时初始化阶段会直接拦截。
 */
#define LCD_ST7789_AVOID_GPIO_13                GPIO_NUM_13

/* LCD_ST7789_SPI_FORCE_GPIO_MATRIX：是否强制 LCD SPI 走 GPIO matrix。
 * 功能：
 *     1：SPI MOSI/SCLK 通过 GPIO matrix 路由到 LCD_ST7789_PIN_MOSI/LCD_ST7789_PIN_SCLK；
 *     0：由 ESP-IDF 根据引脚自动选择 IO_MUX 或 GPIO matrix。
 * 调用方法：
 *     spi_bus_initialize() 的 bus_cfg.flags 会根据该宏追加 SPICOMMON_BUSFLAG_GPIO_PINS。
 */
#define LCD_ST7789_SPI_FORCE_GPIO_MATRIX        1

/* LCD_ST7789_DMA_YIELD_ENABLE：SPI DMA 分块刷屏后是否主动让出调度。
 * 功能：
 *     当前 LCD 使用阻塞式 spi_device_transmit()，阻塞范围只在 LCD 任务内。
 *     置 1 后每个 DMA 分块发送完成会 taskYIELD()，降低连续刷屏对其它任务的影响。
 * 调用方法：
 *     lcd_st7789_flush();     // 内部按该宏决定是否在分块间让出 CPU。
 */
#define LCD_ST7789_DMA_YIELD_ENABLE             1

/* LCD_ST7789_WIDTH：LCD 逻辑显示宽度，单位像素。
 * 功能：
 *     framebuffer 宽度、坐标边界和 UI 布局都会使用该值。
 */
#define LCD_ST7789_WIDTH                        240

/* LCD_ST7789_HEIGHT：LCD 逻辑显示高度，单位像素。
 * 功能：
 *     framebuffer 高度、坐标边界和 UI 布局都会使用该值。
 */
#define LCD_ST7789_HEIGHT                       240

/* LCD_ST7789_X_OFFSET：ST7789 显存 X 起始偏移。
 * 功能：
 *     不同 240x240 模块在 240x320 ST7789 显存中的起点可能不同，显示错位时调整该值。
 */
#define LCD_ST7789_X_OFFSET                     0

/* LCD_ST7789_Y_OFFSET：ST7789 显存 Y 起始偏移。
 * 功能：
 *     常见 240x240 模块可能需要 0/80 等偏移，显示上下错位时调整该值。
 */
#define LCD_ST7789_Y_OFFSET                     0

/* LCD_ST7789_SPI_CLOCK_HZ：LCD SPI 时钟频率，单位 Hz。
 * 功能：
 *     控制 SPI 刷屏速度；20MHz 对飞线和多数模块较稳，走线良好时可提高到 40MHz。
 */
#define LCD_ST7789_SPI_CLOCK_HZ                 (20 * 1000 * 1000)

/* LCD_ST7789_SPI_MODE：ST7789 SPI 模式。
 * 功能：
 *     ST7789 常用 SPI mode 0，即 CPOL=0、CPHA=0。
 */
#define LCD_ST7789_SPI_MODE                     0

/* LCD_ST7789_SPI_QUEUE_SIZE：SPI 设备事务队列深度。
 * 功能：
 *     当前 Driver 使用阻塞发送，队列深度 1 即可，后续可扩展为异步刷屏。
 */
#define LCD_ST7789_SPI_QUEUE_SIZE               1

/* LCD_ST7789_TRANS_TIMEOUT_TICKS：SPI 事务等待 Tick。
 * 功能：
 *     portMAX_DELAY 表示等待当前 SPI 事务完成，保证刷屏稳定。
 */
#define LCD_ST7789_TRANS_TIMEOUT_TICKS          portMAX_DELAY

/* LCD_ST7789_BITS_PER_PIXEL：RGB565 每个像素位数。
 * 功能：
 *     ST7789 当前使用 RGB565，因此每个像素 16bit。
 */
#define LCD_ST7789_BITS_PER_PIXEL               16

/* LCD_ST7789_BYTES_PER_PIXEL：RGB565 每个像素字节数。
 * 功能：
 *     framebuffer 大小和 SPI 发送长度均使用该值。
 */
#define LCD_ST7789_BYTES_PER_PIXEL              2

/* LCD_ST7789_FRAMEBUFFER_SIZE：framebuffer 总大小，单位 Byte。
 * 功能：
 *     240x240x2 = 115200 Byte，需要 DMA-capable 内存。
 */
#define LCD_ST7789_FRAMEBUFFER_SIZE             (LCD_ST7789_WIDTH * LCD_ST7789_HEIGHT * LCD_ST7789_BYTES_PER_PIXEL)

/* LCD_ST7789_MAX_DMA_CHUNK：单次 SPI DMA 发送最大字节数。
 * 功能：
 *     ESP-IDF SPI DMA 单个描述符常见上限为 4092 Byte，这里按 8 行切片发送，保持 3840 Byte 小于上限。
 */
#define LCD_ST7789_MAX_DMA_CHUNK                (LCD_ST7789_WIDTH * LCD_ST7789_BYTES_PER_PIXEL * 8)

/* LCD_ST7789_RESET_LOW_MS：硬件复位低电平保持时间，单位 ms。
 * 功能：
 *     拉低 RST 后等待 ST7789 完成复位识别。
 */
#define LCD_ST7789_RESET_LOW_MS                 20

/* LCD_ST7789_RESET_HIGH_MS：硬件复位释放后等待时间，单位 ms。
 * 功能：
 *     拉高 RST 后等待 ST7789 内部状态稳定。
 */
#define LCD_ST7789_RESET_HIGH_MS                120

/* LCD_ST7789_SLEEP_OUT_DELAY_MS：发送 SLPOUT 后等待时间，单位 ms。
 * 功能：
 *     ST7789 退出睡眠模式后需要等待内部电源和时序稳定。
 */
#define LCD_ST7789_SLEEP_OUT_DELAY_MS           120

/* LCD_ST7789_DISPLAY_ON_DELAY_MS：发送 DISPON 后等待时间，单位 ms。
 * 功能：
 *     显示开启后短暂等待，减少上电首帧花屏概率。
 */
#define LCD_ST7789_DISPLAY_ON_DELAY_MS          20

/* LCD_ST7789_WAIT_TICK_MIN：Driver 延时最小 Tick 数。
 * 功能：
 *     防止较短毫秒延时在低 tick 频率下被转换成 0，导致初始化时序不稳定。
 */
#define LCD_ST7789_WAIT_TICK_MIN                1

/* LCD_ST7789_MADCTL_VALUE：屏幕扫描方向和 RGB/BGR 顺序控制值。
 * 功能：
 *     写入 MADCTL 寄存器；默认 0x00 表示常规竖屏 RGB 顺序。
 * 调试说明：
 *     若颜色红蓝颠倒，可尝试打开 LCD_ST7789_MADCTL_BGR；若方向不对，可组合 MX/MY/MV 位。
 */
#define LCD_ST7789_MADCTL_VALUE                 0x00

/* LCD_ST7789_COLOR_MODE_VALUE：像素格式寄存器值。
 * 功能：
 *     0x55 表示 16bit/pixel RGB565，是当前 framebuffer 使用的格式。
 */
#define LCD_ST7789_COLOR_MODE_VALUE             0x55

/* LCD_ST7789_INVERSION_ENABLE：是否开启显示反色模式。
 * 功能：
 *     1：发送 INVON，部分 ST7789 模块需要开启反色后颜色才正常；
 *     0：发送 INVOFF。
 */
#define LCD_ST7789_INVERSION_ENABLE             1

/* ============================== ST7789 命令宏定义 ============================== */

#define LCD_ST7789_CMD_NOP                      0x00    /* 空操作命令，当前仅保留用于调试 */
#define LCD_ST7789_CMD_SWRESET                  0x01    /* 软件复位命令 */
#define LCD_ST7789_CMD_SLPOUT                   0x11    /* 退出睡眠模式命令 */
#define LCD_ST7789_CMD_INVOFF                   0x20    /* 关闭显示反色命令 */
#define LCD_ST7789_CMD_INVON                    0x21    /* 开启显示反色命令 */
#define LCD_ST7789_CMD_DISPON                   0x29    /* 开启显示命令 */
#define LCD_ST7789_CMD_CASET                    0x2A    /* 设置列地址窗口命令 */
#define LCD_ST7789_CMD_RASET                    0x2B    /* 设置行地址窗口命令 */
#define LCD_ST7789_CMD_RAMWR                    0x2C    /* 写显存命令 */
#define LCD_ST7789_CMD_MADCTL                   0x36    /* 内存访问控制命令，用于屏幕方向/RGB 顺序 */
#define LCD_ST7789_CMD_COLMOD                   0x3A    /* 像素格式设置命令 */
#define LCD_ST7789_CMD_NORON                    0x13    /* 普通显示模式开启命令 */

/* ============================== RGB565 常用颜色 ============================== */

#define LCD_COLOR_BLACK                         0x0000  /* 黑色，常用于背景 */
#define LCD_COLOR_WHITE                         0xFFFF  /* 白色，常用于主文字 */
#define LCD_COLOR_RED                           0xF800  /* 红色，常用于异常状态 */
#define LCD_COLOR_GREEN                         0x07E0  /* 绿色，常用于正常状态 */
#define LCD_COLOR_BLUE                          0x001F  /* 蓝色，常用于强调 */
#define LCD_COLOR_YELLOW                        0xFFE0  /* 黄色，常用于提示 */
#define LCD_COLOR_CYAN                          0x07FF  /* 青色，常用于数值 */
#define LCD_COLOR_MAGENTA                       0xF81F  /* 品红色，保留给后续状态提示 */
#define LCD_COLOR_GRAY                          0x8410  /* 灰色，常用于辅助文字 */
#define LCD_COLOR_DARK_GRAY                     0x4208  /* 深灰色，常用于分割线和背景块 */
#define LCD_COLOR_ORANGE                        0xFD20  /* 橙色，常用于气体电阻等辅助指标 */
#define LCD_COLOR_NAVY                          0x0010  /* 深蓝色，当前保留用于后续主题 */

/* ============================== 字体可调参数区 ============================== */

/* LCD_FONT_BASE_WIDTH：基础 ASCII 点阵宽度，单位像素。
 * 功能：
 *     当前字体使用 5x7 基础点阵，再由 font16/font24 按比例放大。
 */
#define LCD_FONT_BASE_WIDTH                     5

/* LCD_FONT_BASE_HEIGHT：基础 ASCII 点阵高度，单位像素。
 * 功能：
 *     当前字体使用 7 行基础点阵，后续替换字体时优先同步修改该宏。
 */
#define LCD_FONT_BASE_HEIGHT                    7

/* LCD_FONT16_WIDTH：font16 单字符占位宽度，单位像素。
 * 功能：
 *     UI 标签、状态文本默认使用该宽度进行排版。
 */
#define LCD_FONT16_WIDTH                        8

/* LCD_FONT16_HEIGHT：font16 单字符占位高度，单位像素。
 * 功能：
 *     5x7 基础点阵按 Y 方向放大 2 倍并增加留白，最终占 8x16。
 */
#define LCD_FONT16_HEIGHT                       16

/* LCD_FONT16_SCALE_X：font16 基础点阵 X 方向放大倍数。
 * 功能：
 *     保持 1 表示横向不放大，文字更适合小标签。
 */
#define LCD_FONT16_SCALE_X                      1

/* LCD_FONT16_SCALE_Y：font16 基础点阵 Y 方向放大倍数。
 * 功能：
 *     5x7 点阵纵向放大 2 倍，提升 240x240 屏幕上的可读性。
 */
#define LCD_FONT16_SCALE_Y                      2

/* LCD_FONT16_OFFSET_X：font16 点阵在字符框内的 X 偏移。
 * 功能：
 *     给每个字符左侧留 1 像素边距，避免字符贴边。
 */
#define LCD_FONT16_OFFSET_X                     1

/* LCD_FONT16_OFFSET_Y：font16 点阵在字符框内的 Y 偏移。
 * 功能：
 *     给每个字符顶部留 1 像素边距，使文本垂直观感更稳。
 */
#define LCD_FONT16_OFFSET_Y                     1

/* LCD_FONT24_WIDTH：font24 单字符占位宽度，单位像素。
 * 功能：
 *     Dashboard 主要数值默认使用该宽度进行排版。
 */
#define LCD_FONT24_WIDTH                        16

/* LCD_FONT24_HEIGHT：font24 单字符占位高度，单位像素。
 * 功能：
 *     5x7 基础点阵按 2x3 放大并增加留白，最终占 16x24。
 */
#define LCD_FONT24_HEIGHT                       24

/* LCD_FONT24_SCALE_X：font24 基础点阵 X 方向放大倍数。
 * 功能：
 *     5 列基础点阵横向放大 2 倍，适合显示主要数值。
 */
#define LCD_FONT24_SCALE_X                      2

/* LCD_FONT24_SCALE_Y：font24 基础点阵 Y 方向放大倍数。
 * 功能：
 *     7 行基础点阵纵向放大 3 倍，适合显示主要数值。
 */
#define LCD_FONT24_SCALE_Y                      3

/* LCD_FONT24_OFFSET_X：font24 点阵在字符框内的 X 偏移。
 * 功能：
 *     在 16 像素字符框内水平居中 10 像素实际点阵。
 */
#define LCD_FONT24_OFFSET_X                     3

/* LCD_FONT24_OFFSET_Y：font24 点阵在字符框内的 Y 偏移。
 * 功能：
 *     在 24 像素字符框内垂直放置 21 像素实际点阵。
 */
#define LCD_FONT24_OFFSET_Y                     1

/* ============================== 字体接口数据结构 ============================== */

typedef const uint8_t *(*lcd_font_get_glyph_cb_t)(char ch);

typedef struct _lcd_font_t
{
    uint8_t width;                         /* 字符占位宽度，单位像素 */
    uint8_t height;                        /* 字符占位高度，单位像素 */
    uint8_t base_width;                    /* 基础点阵宽度，当前为 5 */
    uint8_t base_height;                   /* 基础点阵高度，当前为 7 */
    uint8_t scale_x;                       /* 基础点阵 X 方向放大倍数 */
    uint8_t scale_y;                       /* 基础点阵 Y 方向放大倍数 */
    uint8_t offset_x;                      /* 点阵在字符占位框内的 X 偏移 */
    uint8_t offset_y;                      /* 点阵在字符占位框内的 Y 偏移 */
    lcd_font_get_glyph_cb_t get_glyph;     /* 根据 ASCII 字符获取 5x7 点阵的回调 */
} lcd_font_t;

/* lcd_font_get_basic_5x7_glyph：获取基础 5x7 ASCII 点阵。
 *
 * 参数：
 *     ch：需要显示的 ASCII 字符，支持数字、英文字母、空格和常用符号。
 *
 * 返回：
 *     7 字节点阵地址；每个字节低 5bit 表示一行像素，未知字符返回空格点阵。
 *
 * 调用方法：
 *     const uint8_t *glyph = lcd_font_get_basic_5x7_glyph('A');
 *
 * 说明：
 *     该函数由 font16.c 实现，font16/font24 通过不同缩放参数复用同一套点阵。
 */
const uint8_t *lcd_font_get_basic_5x7_glyph(char ch);

/* ============================== Driver 配置和接口 ============================== */

typedef struct _lcd_st7789_config_t
{
    spi_host_device_t host;                /* SPI 控制器，例如 LCD_ST7789_HOST */
    spi_dma_chan_t dma_chan;               /* SPI DMA 通道，例如 LCD_ST7789_SPI_DMA_CH */
    gpio_num_t pin_mosi;                   /* SPI MOSI GPIO */
    gpio_num_t pin_sclk;                   /* SPI SCLK GPIO */
    gpio_num_t pin_cs;                     /* SPI CS GPIO，可为 GPIO_NUM_NC */
    gpio_num_t pin_dc;                     /* D/C GPIO，必须有效 */
    gpio_num_t pin_rst;                    /* RST GPIO，可为 GPIO_NUM_NC */
    gpio_num_t pin_bl;                     /* 背光 GPIO，可为 GPIO_NUM_NC */
    int bl_on_level;                       /* 背光点亮电平，通常为 1 */
    uint16_t width;                        /* 逻辑显示宽度，单位像素 */
    uint16_t height;                       /* 逻辑显示高度，单位像素 */
    uint16_t x_offset;                     /* 显存 X 偏移 */
    uint16_t y_offset;                     /* 显存 Y 偏移 */
    int spi_clock_hz;                      /* SPI 时钟频率，单位 Hz */
} lcd_st7789_config_t;

/* lcd_st7789_get_default_config：获取 ST7789 Driver 默认配置。
 *
 * 参数：
 *     config：输出配置指针，不能为 NULL。
 *
 * 返回：
 *     ESP_OK：获取成功；
 *     ESP_ERR_INVALID_ARG：config 为 NULL。
 *
 * 调用方法：
 *     lcd_st7789_config_t cfg = {0};
 *     ESP_ERROR_CHECK(lcd_st7789_get_default_config(&cfg));
 */
esp_err_t lcd_st7789_get_default_config(lcd_st7789_config_t *config);

/* lcd_st7789_init：初始化 ST7789 Driver。
 *
 * 功能：
 *     初始化 GPIO、SPI Master、DMA framebuffer，并完成 ST7789 寄存器初始化。
 *
 * 参数：
 *     config：Driver 配置；传 NULL 时使用默认配置。
 *
 * 返回：
 *     ESP_OK：初始化成功；
 *     其它值：GPIO/SPI/framebuffer/芯片初始化失败。
 *
 * 调用方法：
 *     ESP_ERROR_CHECK(lcd_st7789_init(NULL));
 */
esp_err_t lcd_st7789_init(const lcd_st7789_config_t *config);

/* lcd_st7789_draw_pixel：在 framebuffer 中绘制一个像素。
 *
 * 参数：
 *     x：像素 X 坐标，范围 0 ~ width-1。
 *     y：像素 Y 坐标，范围 0 ~ height-1。
 *     color：RGB565 颜色值，例如 LCD_COLOR_WHITE。
 *
 * 返回：
 *     ESP_OK：绘制成功；
 *     ESP_ERR_INVALID_STATE：Driver 尚未初始化；
 *     ESP_ERR_INVALID_ARG：坐标越界。
 *
 * 调用方法：
 *     lcd_st7789_draw_pixel(10, 20, LCD_COLOR_WHITE);
 */
esp_err_t lcd_st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t color);

/* lcd_st7789_fill_screen：用指定颜色填充整个 framebuffer。
 *
 * 参数：
 *     color：RGB565 颜色值。
 *
 * 返回：
 *     ESP_OK：填充成功；
 *     ESP_ERR_INVALID_STATE：Driver 尚未初始化。
 *
 * 调用方法：
 *     lcd_st7789_fill_screen(LCD_COLOR_BLACK);
 */
esp_err_t lcd_st7789_fill_screen(uint16_t color);

/* lcd_st7789_draw_char：在 framebuffer 中绘制单个 ASCII 字符。
 *
 * 参数：
 *     x/y：字符左上角坐标。
 *     ch：需要绘制的 ASCII 字符，支持数字、英文字母和常见符号。
 *     font：字体对象，例如 &g_lcd_font16；传 NULL 会返回参数错误。
 *     color：文字颜色。
 *     bg_color：背景颜色。
 *
 * 返回：
 *     ESP_OK：绘制成功；
 *     其它值：参数错误或 Driver 未初始化。
 *
 * 调用方法：
 *     lcd_st7789_draw_char(0, 0, 'A', &g_lcd_font16, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
 */
esp_err_t lcd_st7789_draw_char(uint16_t x,
                               uint16_t y,
                               char ch,
                               const lcd_font_t *font,
                               uint16_t color,
                               uint16_t bg_color);

/* lcd_st7789_draw_string：在 framebuffer 中绘制 ASCII 字符串。
 *
 * 参数：
 *     x/y：字符串左上角坐标。
 *     str：以 '\0' 结尾的字符串，不能为 NULL。
 *     font：字体对象。
 *     color：文字颜色。
 *     bg_color：背景颜色。
 *
 * 返回：
 *     ESP_OK：绘制成功；
 *     其它值：参数错误或 Driver 未初始化。
 *
 * 调用方法：
 *     lcd_st7789_draw_string(8, 8, "TEMP", &g_lcd_font16, LCD_COLOR_WHITE, LCD_COLOR_BLACK);
 */
esp_err_t lcd_st7789_draw_string(uint16_t x,
                                 uint16_t y,
                                 const char *str,
                                 const lcd_font_t *font,
                                 uint16_t color,
                                 uint16_t bg_color);

/* lcd_st7789_draw_number：格式化并绘制浮点数。
 *
 * 参数：
 *     x/y：数字左上角坐标。
 *     value：需要显示的数值。
 *     decimals：小数位数，0 表示整数显示，最大建议 3。
 *     font：字体对象。
 *     color：文字颜色。
 *     bg_color：背景颜色。
 *
 * 返回：
 *     ESP_OK：绘制成功；
 *     其它值：格式化失败、参数错误或 Driver 未初始化。
 *
 * 调用方法：
 *     lcd_st7789_draw_number(92, 42, 26.5f, 1, &g_lcd_font24, LCD_COLOR_CYAN, LCD_COLOR_BLACK);
 */
esp_err_t lcd_st7789_draw_number(uint16_t x,
                                 uint16_t y,
                                 float value,
                                 uint8_t decimals,
                                 const lcd_font_t *font,
                                 uint16_t color,
                                 uint16_t bg_color);

/* lcd_st7789_flush：把 framebuffer 通过 SPI DMA 发送到 ST7789。
 *
 * 返回：
 *     ESP_OK：刷屏成功；
 *     ESP_ERR_INVALID_STATE：Driver 尚未初始化；
 *     其它值：SPI 发送失败。
 *
 * 调用方法：
 *     lcd_st7789_fill_screen(LCD_COLOR_BLACK);
 *     lcd_st7789_draw_string(...);
 *     ESP_ERROR_CHECK(lcd_st7789_flush());
 */
esp_err_t lcd_st7789_flush(void);

/* lcd_st7789_get_width：获取当前 LCD 逻辑宽度。
 *
 * 返回：
 *     当前宽度；Driver 未初始化时返回默认宏 LCD_ST7789_WIDTH。
 *
 * 调用方法：
 *     uint16_t width = lcd_st7789_get_width();
 */
uint16_t lcd_st7789_get_width(void);

/* lcd_st7789_get_height：获取当前 LCD 逻辑高度。
 *
 * 返回：
 *     当前高度；Driver 未初始化时返回默认宏 LCD_ST7789_HEIGHT。
 *
 * 调用方法：
 *     uint16_t height = lcd_st7789_get_height();
 */
uint16_t lcd_st7789_get_height(void);

/* lcd_st7789_is_initialized：查询 Driver 是否已初始化。
 *
 * 返回：
 *     true：ST7789 Driver 已可绘制和刷屏；
 *     false：尚未初始化。
 *
 * 调用方法：
 *     if (lcd_st7789_is_initialized()) {
 *         lcd_st7789_flush();
 *     }
 */
bool lcd_st7789_is_initialized(void);

extern const lcd_font_t g_lcd_font16;       /* 8x16 ASCII 字体，适合标签和状态文本 */
extern const lcd_font_t g_lcd_font24;       /* 16x24 ASCII 字体，适合 Dashboard 大号数值 */

#endif
