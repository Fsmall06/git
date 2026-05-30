#ifndef __ENV_H
#define __ENV_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

/* ============================== ENV 可调参数区 ==============================
 *
 * 说明：
 *     1. 本区域集中保存 ENV 模块后续调试可能会改动的参数。
 *     2. 业务代码不要在 env.c 中散落魔法数字，后续调试优先修改本头文件宏定义。
 *     3. ENV 模块只通过 BSP/BME690 的公开接口获取传感器数据，保持模块相对独立，便于移植。
 */

/* ENV_TASK_NAME：环境采样任务名称。
 * 功能：
 *     FreeRTOS 创建任务时使用该名字，方便在日志和调试工具中识别任务。
 * 调用方法：
 *     env_init();     // 内部会使用 ENV_TASK_NAME 创建环境采样任务
 */
#define ENV_TASK_NAME                         "env_task"

/* ENV_TASK_STACK_SIZE：环境采样任务栈大小，单位 Byte。
 * 功能：
 *     BME690 读取过程会使用 I2C、日志和浮点计算，默认预留 4096 Byte。
 * 调试建议：
 *     如果后续在 ENV 任务中增加滤波、算法或更多日志，可适当增大该值。
 */
#define ENV_TASK_STACK_SIZE                   4096

/* ENV_TASK_PRIORITY：环境采样任务优先级。
 * 功能：
 *     控制环境采样任务在 FreeRTOS 调度中的优先级。
 * 调试建议：
 *     环境数据通常不需要高实时性，默认优先级 5 即可。
 */
#define ENV_TASK_PRIORITY                     5

/* ENV_TASK_CORE_ID：环境采样任务绑定 CPU 核心。
 * 功能：
 *     使用 ESP-IDF v5 的 xTaskCreatePinnedToCore() 创建任务时指定运行核心。
 * 取值：
 *     tskNO_AFFINITY：不绑定核心，由 FreeRTOS 自动调度。
 *     0/1：绑定到指定核心，具体是否支持取决于当前芯片。
 */
#define ENV_TASK_CORE_ID                      tskNO_AFFINITY

/* ENV_SAMPLE_PERIOD_MS：环境数据采样周期，单位 ms。
 * 功能：
 *     ENV 任务每隔该时间调用一次 bme690_read()。
 * 调试建议：
 *     BME690 的温湿度、气压、气体数据变化较慢，默认 3000ms 兼顾实时性和功耗。
 */
#define ENV_SAMPLE_PERIOD_MS                  3000

/* ENV_LOG_PERIOD_MS：环境数据日志打印周期，单位 ms。
 * 功能：
 *     ENV 任务按该周期打印一次最新环境数据，避免每次采样都刷屏。
 * 调试建议：
 *     调试阶段可改小，例如 3000；量产或长期运行可改大或关闭日志。
 */
#define ENV_LOG_PERIOD_MS                     10000

/* ENV_LOG_ENABLE：是否开启 ENV 模块周期日志。
 * 取值：
 *     1：开启周期打印环境数据；
 *     0：关闭周期打印，仅保存最新数据供 env_get_data() 获取。
 */
#define ENV_LOG_ENABLE                        1

/* ENV_INIT_RETRY_DELAY_MS：BME690 初始化失败后的重试间隔，单位 ms。
 * 功能：
 *     如果上电时传感器未准备好，ENV 任务会按该间隔重新尝试初始化。
 */
#define ENV_INIT_RETRY_DELAY_MS               2000

/* ENV_READ_FAIL_LOG_PERIOD_MS：读取失败日志最小间隔，单位 ms。
 * 功能：
 *     BME690 连续读取失败时限制错误日志频率，避免串口日志过多影响系统运行。
 */
#define ENV_READ_FAIL_LOG_PERIOD_MS           5000

/* ENV_GET_DATA_REQUIRE_VALID：env_get_data() 是否要求已有有效采样数据。
 * 取值：
 *     1：还没有成功采样时 env_get_data() 返回 ESP_ERR_INVALID_STATE；
 *     0：即使还没有成功采样也返回当前缓存内容，调用者通过 valid 字段判断。
 */
#define ENV_GET_DATA_REQUIRE_VALID            1

/* ============================== ENV 数据结构定义 ============================== */

typedef struct _env_data_t
{
    float temperature_c;              /* 温度，单位：摄氏度。来自 BME690 温度补偿结果。 */
    float humidity_percent;           /* 相对湿度，单位：%RH。来自 BME690 湿度补偿结果。 */
    float pressure_pa;                /* 气压，单位：Pa。来自 BME690 气压补偿结果。 */
    float pressure_hpa;               /* 气压，单位：hPa。便于日志显示和常规业务使用。 */
    uint32_t gas_resistance_ohm;      /* 气体电阻，单位：Ohm。用于空气质量趋势判断。 */
    bool gas_valid;                   /* 气体电阻是否有效。true 表示 BME690 标记 gas 数据有效。 */
    bool heat_stable;                 /* 加热器是否稳定。true 表示 BME690 标记 heater 已稳定。 */
    bool valid;                       /* ENV 缓存数据是否有效。第一次成功读取后置 true。 */
    uint32_t sample_count;            /* 成功采样次数。用于判断数据是否持续更新。 */
    uint32_t error_count;             /* 采样失败次数。用于观察传感器通信稳定性。 */
    uint64_t timestamp_ms;            /* 本次数据更新时间，单位 ms，来自 esp_timer_get_time()。 */
} env_data_t;

/* ============================== ENV 对外接口声明 ============================== */

/* env_init：初始化 ENV 环境采样模块。
 *
 * 功能：
 *     1. 初始化 BSP/BME690；
 *     2. 创建 FreeRTOS 环境采样任务；
 *     3. 任务会周期读取 BME690 数据、保存最新环境数据并定时打印日志。
 *
 * 返回：
 *     ESP_OK：ENV 模块初始化成功，采样任务已创建或此前已创建；
 *     其它值：初始化失败，通常为 BME690 初始化失败或任务创建失败。
 *
 * 调用方法：
 *     esp_err_t ret = env_init();
 *     if (ret != ESP_OK) {
 *         // 根据 ret 做错误处理
 *     }
 *
 * 注意：
 *     env_init() 可以重复调用。首次成功后再次调用会直接返回 ESP_OK，不会重复创建任务。
 */
esp_err_t env_init(void);

/* env_get_data：获取 ENV 模块缓存的最新环境数据。
 *
 * 功能：
 *     将 ENV 采样任务最近一次成功读取到的数据复制给调用者。
 *
 * 参数：
 *     data：输出参数，用于保存最新环境数据，不能为 NULL。
 *
 * 返回：
 *     ESP_OK：获取成功；
 *     ESP_ERR_INVALID_ARG：data 为 NULL；
 *     ESP_ERR_INVALID_STATE：ENV 尚未初始化，或当前还没有有效采样数据。
 *
 * 调用方法：
 *     env_data_t env = {0};
 *     esp_err_t ret = env_get_data(&env);
 *     if (ret == ESP_OK && env.valid) {
 *         // 使用 env.temperature_c / env.humidity_percent / env.pressure_hpa 等数据
 *     }
 */
esp_err_t env_get_data(env_data_t *data);

#endif
