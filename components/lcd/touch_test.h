#ifndef __TOUCH_TEST_H
#define __TOUCH_TEST_H

#include <stdint.h>

#include "esp_err.h"
#include "iic.h"

/* ============================== 触摸测试可调参数区 ============================== */

/* TOUCH_TEST_IIC_PORT：触摸测试模块复用的 I2C 控制器编号。
 *
 * 功能：
 *     指定 touch_test_probe() 使用哪一路现有 BSP/IIC 总线访问触摸芯片。
 *
 * 默认值：
 *     默认复用 IIC_MASTER_PORT，也就是当前工程里 BME690 扫描到的同一条 I2C 总线。
 *
 * 调用方法：
 *     touch_test_probe();        // 内部会调用 iic_init(TOUCH_TEST_IIC_PORT)
 *
 * 后期调试：
 *     如果触摸芯片后续迁移到其它 I2C 控制器，只需要优先修改本宏，
 *     同时确认 BSP/IIC 中已经配置了对应控制器的 SDA/SCL 引脚。
 */
#define TOUCH_TEST_IIC_PORT                 IIC_MASTER_PORT

/* TOUCH_TEST_IIC_ADDR：CST816S 触摸芯片的 7bit I2C 地址。
 *
 * 功能：
 *     touch_test_probe() 会向该地址发送寄存器地址，并读取 1 字节返回值。
 *
 * 当前配置：
 *     根据本项目当前 I2C 扫描结果，0x15 极有可能是 LCD 电容触摸芯片 CST816S。
 *
 * 后期调试：
 *     如果更换触摸芯片或硬件地址发生变化，只需要修改本宏，不需要改 touch_test.c。
 */
#define TOUCH_TEST_IIC_ADDR                 0x15U

/* TOUCH_TEST_REG_CHIP_ID：触摸芯片待验证寄存器 0xA7。
 *
 * 功能：
 *     touch_test_probe() 第 1 个读取该寄存器，并按
 *     "TOUCH: reg 0xA7 = 0xXX" 的格式打印读取结果。
 *
 * 调用方法：
 *     touch_test_probe();        // 函数内部自动读取本宏指定的寄存器
 */
#define TOUCH_TEST_REG_A7                   0xA7U

/* TOUCH_TEST_REG_PROJ_ID：触摸芯片待验证寄存器 0xA8。
 *
 * 功能：
 *     touch_test_probe() 第 2 个读取该寄存器，并按
 *     "TOUCH: reg 0xA8 = 0xXX" 的格式打印读取结果。
 *
 * 后期调试：
 *     如果需要验证其它只读寄存器，可修改本宏指向新的寄存器地址。
 */
#define TOUCH_TEST_REG_A8                   0xA8U

/* TOUCH_TEST_REG_FW_VER：触摸芯片待验证寄存器 0xA9。
 *
 * 功能：
 *     touch_test_probe() 第 3 个读取该寄存器，并按
 *     "TOUCH: reg 0xA9 = 0xXX" 的格式打印读取结果。
 *
 * 后期调试：
 *     如果后续数据手册确认该寄存器含义不同，不影响本次探测流程；
 *     只需要在注释或上层解析中调整说明。
 */
#define TOUCH_TEST_REG_A9                   0xA9U

/* TOUCH_TEST_READ_LEN：每个触摸寄存器读取的数据长度，单位字节。
 *
 * 功能：
 *     当前测试要求每个寄存器只读取 1 字节，因此默认值固定为 1。
 *
 * 后期调试：
 *     如果后续要读取连续寄存器，可以修改本宏并同步扩展 touch_test.c 的缓冲区处理。
 */
#define TOUCH_TEST_READ_LEN                 1U

/* TOUCH_TEST_REG_COUNT：本次触摸测试需要读取的寄存器数量。
 *
 * 功能：
 *     touch_test_probe() 使用该宏控制寄存器表长度，避免在代码里写死循环次数。
 *
 * 后期调试：
 *     如果新增或删除测试寄存器，需要同步调整本宏和 touch_test.c 中的寄存器数组。
 */
#define TOUCH_TEST_REG_COUNT                3U

/* TOUCH_TEST_LOG_TAG：触摸测试模块日志 TAG。
 *
 * 功能：
 *     用于 ESP_LOGI/ESP_LOGE 的模块标识，方便串口日志中过滤触摸测试输出。
 *
 * 注意：
 *     用户要求的日志正文是 "TOUCH: reg ..." 和 "TOUCH: read failed"，
 *     本宏只影响 ESP-IDF 日志前缀，不影响日志正文。
 */
#define TOUCH_TEST_LOG_TAG                  "TOUCH_TEST"

/* ============================== 函数声明 ============================== */

/* touch_test_probe：读取 0x15 触摸芯片的 0xA7/0xA8/0xA9 寄存器。
 *
 * 功能：
 *     1. 复用现有 BSP/IIC 驱动初始化 I2C 总线；
 *     2. 使用 7bit I2C 地址 TOUCH_TEST_IIC_ADDR，也就是当前扫描到的 0x15；
 *     3. 依次读取 TOUCH_TEST_REG_A7、TOUCH_TEST_REG_A8、TOUCH_TEST_REG_A9；
 *     4. 每个寄存器读取 TOUCH_TEST_READ_LEN 字节；
 *     5. 成功时打印：
 *            TOUCH: reg 0xA7 = 0xXX
 *            TOUCH: reg 0xA8 = 0xXX
 *            TOUCH: reg 0xA9 = 0xXX
 *        任意一步读取失败时打印：
 *            TOUCH: read failed
 *
 * 调用方法：
 *     esp_err_t ret = lcd_init();
 *     if (ret == ESP_OK) {
 *         touch_test_probe();
 *     }
 *
 * 返回：
 *     ESP_OK：3 个寄存器全部读取成功；
 *     其它值：I2C 初始化失败、I2C 读取失败或参数配置异常。
 */
esp_err_t touch_test_probe(void);

#endif
