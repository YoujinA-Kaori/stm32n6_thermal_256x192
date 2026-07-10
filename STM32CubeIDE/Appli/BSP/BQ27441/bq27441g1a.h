#ifndef BQ27441_H
#define BQ27441_H

#include "stm32n6xx_hal.h"

// 默认 I2C 地址（7 位）
#define BQ27441_DEFAULT_ADDR  0x55

// 标准命令码 (单字节命令，读取2字节数据)
#define BQ27441_CMD_FLAGS         0x06	// 状态标志
#define BQ27441_CMD_VOLTAGE       0x04	// 电压 (mV)
#define BQ27441_CMD_TEMP          0x02	// 温度 (0.1°K)
#define BQ27441_CMD_AVG_CURRENT   0x10	// 平均电流 (mA)
#define BQ27441_CMD_SOC           0x1C	// 电量百分比(% 0~10000对应0.00%~100.00%)

#define CURRENT_THRESHOLD_MA 0.01f		// 电池状态监测阈值

// 充放电状态
typedef enum
{
    BQ27441_CHARGE_STATUS_CHARGING    = 0,	// 充电中
    BQ27441_CHARGE_STATUS_DISCHARGING = 1,	// 放电中
    BQ27441_CHARGE_STATUS_SLEEP       = 2 	// 休眠/待机（无充放电）
} BQ27441_ChargeStatusTypeDef;

// 设备结构体
typedef struct
{
    I2C_HandleTypeDef *hi2c;	// I2C 句柄
    uint16_t addr;				// 设备地址 (7 位)
    uint32_t timeout_ms;		// I2C 超时时间 (ms)

    // 测量结果
    float voltage;				// 电池电压 (V)
    float temperature;			// 电池温度 (℃)
    float current;				// 平均电流 (A，充电为正，放电为负)
    uint8_t soc;				// 电量百分比 (0~100)
    BQ27441_ChargeStatusTypeDef charge_status; // 充放电状态
} BQ27441_Device_t;

// BQ27441初始化
HAL_StatusTypeDef BQ27441_Init(BQ27441_Device_t *dev, I2C_HandleTypeDef *hi2c, uint16_t addr, uint32_t timeout_ms);

// 批量更新BQ27441所有测量数据
HAL_StatusTypeDef BQ27441_Update(BQ27441_Device_t *dev);

#endif /* BQ27441_H */
