#ifndef __TOUCH_SCAN_H
#define __TOUCH_SCAN_H

#include <stdint.h>

#include "esp_err.h"
#include "iic.h"

/* ============================== Touch Scan 可调参数区 ============================== */

/* TOUCH_SCAN_IIC_PORT：touch_scan 使用的 I2C 控制器编号。
 *
 * 功能：
 *     指定本模块复用哪一路现有 BSP/IIC 总线。
 *
 * 默认值：
 *     默认复用 IIC_MASTER_PORT，也就是工程现有 IIC 驱动默认初始化的 I2C 控制器。
 *
 * 调用方法：
 *     touch_scan_run();       // 内部会调用 iic_init(TOUCH_SCAN_IIC_PORT)
 *
 * 后期调试：
 *     如果触摸芯片接到其它 I2C 控制器，优先修改本宏，同时确认 BSP/IIC 已经配置对应 SDA/SCL。
 */
#define TOUCH_SCAN_IIC_PORT                 IIC_MASTER_PORT

/* TOUCH_SCAN_ADDR_START：触摸候选设备扫描起始地址。
 *
 * 功能：
 *     touch_scan_run() 会从该地址开始扫描 I2C 7bit 地址。
 *
 * 当前要求：
 *     按测试要求从 0x08 开始扫描。
 *
 * 调用方法：
 *     touch_scan_run();       // 内部扫描范围包含 TOUCH_SCAN_ADDR_START
 */
#define TOUCH_SCAN_ADDR_START               0x08U

/* TOUCH_SCAN_ADDR_END：触摸候选设备扫描结束地址。
 *
 * 功能：
 *     touch_scan_run() 会扫描到该地址为止，范围包含本地址。
 *
 * 当前要求：
 *     按测试要求扫描到 0x77。
 *
 * 调用方法：
 *     touch_scan_run();       // 内部扫描范围包含 TOUCH_SCAN_ADDR_END
 */
#define TOUCH_SCAN_ADDR_END                 0x77U

/* TOUCH_SCAN_PROBE_TIMEOUT_MS：扫描单个 I2C 地址时的探测超时时间，单位 ms。
 *
 * 功能：
 *     传给 ESP-IDF 的 i2c_master_probe()，用于判断某个地址是否有设备 ACK。
 *
 * 默认值：
 *     复用现有 BSP/IIC 的 IIC_SCAN_TIMEOUT_MS，避免 touch_scan 自己维护一套 I2C 超时策略。
 *
 * 后期调试：
 *     如果触摸芯片响应较慢，可以适当调大；如果希望缩短完整扫描时间，可以适当调小。
 */
#define TOUCH_SCAN_PROBE_TIMEOUT_MS         IIC_SCAN_TIMEOUT_MS

/* TOUCH_SCAN_LOG_TAG：touch_scan 日志 TAG。
 *
 * 功能：
 *     用于 ESP_LOGI/ESP_LOGE 的模块标识，便于串口日志中过滤 touch_scan 输出。
 *
 * 后期调试：
 *     如果项目中有多个触摸测试模块，可以修改该宏让日志标签更明确。
 */
#define TOUCH_SCAN_LOG_TAG                  "TOUCH_SCAN"

/* ============================== 函数声明 ============================== */

/* touch_scan_run：扫描 I2C 总线上的触摸候选设备地址。
 *
 * 功能：
 *     1. 复用现有 BSP/IIC 驱动，调用 iic_init(TOUCH_SCAN_IIC_PORT) 初始化 I2C 总线；
 *     2. 在 TOUCH_SCAN_ADDR_START ~ TOUCH_SCAN_ADDR_END 范围内逐个探测 7bit I2C 地址；
 *     3. 每发现一个有 ACK 响应的地址，就打印：
 *            Touch candidate found: 0xXX
 *
 * 调用方法：
 *     esp_err_t ret = touch_scan_run();
 *     if (ret != ESP_OK) {
 *         // 根据 esp_err_to_name(ret) 或日志继续排查 I2C 初始化/参数问题
 *     }
 *
 * 返回：
 *     ESP_OK：扫描流程执行完成，即使没有发现设备也返回 ESP_OK；
 *     其它值：I2C 初始化失败、地址配置错误或总线状态异常。
 */
esp_err_t touch_scan_run(void);

#endif
