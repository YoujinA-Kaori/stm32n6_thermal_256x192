#include "bq27441g1a.h"
#include "i2c.h"

/**
 * @brief Read one 16-bit BQ27441 register over I2C.
 * @param dev BQ27441 device handle.
 * @param cmd Register command.
 * @param data Output value buffer.
 * @return HAL_StatusTypeDef HAL status from the transfer.
 */
static HAL_StatusTypeDef BQ27441_ReadWord(BQ27441_Device_t *dev, uint8_t cmd, uint16_t *data)
{
    uint8_t rx_data[2];
    HAL_StatusTypeDef hal_status;

    hal_status = HAL_I2C_Mem_Read(dev->hi2c,
                                  (uint16_t)(dev->addr << 1),
                                  cmd,
                                  I2C_MEMADD_SIZE_8BIT,
                                  rx_data,
                                  2U,
                                  dev->timeout_ms);
    if (hal_status != HAL_OK)
    {
        app_i2c4_bus_recover_locked();
        return hal_status;
    }

    *data = ((uint16_t)rx_data[1] << 8) | rx_data[0];
    return HAL_OK;
}

/**
 * @brief Initialize the BQ27441 device handle and verify I2C access.
 * @param dev BQ27441 device handle.
 * @param hi2c I2C handle used by the gauge.
 * @param addr 7-bit I2C device address.
 * @param timeout_ms Blocking I2C timeout in milliseconds.
 * @return HAL_StatusTypeDef HAL status.
 */
HAL_StatusTypeDef BQ27441_Init(BQ27441_Device_t *dev, I2C_HandleTypeDef *hi2c, uint16_t addr, uint32_t timeout_ms)
{
    uint16_t test_value;

    if ((dev == NULL) || (hi2c == NULL))
    {
        return HAL_ERROR;
    }

    dev->hi2c = hi2c;
    dev->addr = addr;
    dev->timeout_ms = timeout_ms;

    if (BQ27441_ReadWord(dev, BQ27441_CMD_VOLTAGE, &test_value) != HAL_OK)
    {
        return HAL_ERROR;
    }

    dev->voltage = 0.0f;
    dev->temperature = 0.0f;
    dev->current = 0.0f;
    dev->soc = 0U;
    dev->charge_status = BQ27441_CHARGE_STATUS_SLEEP;

    return HAL_OK;
}

/**
 * @brief Refresh the cached battery measurements from the gauge.
 * @param dev BQ27441 device handle.
 * @return HAL_StatusTypeDef HAL status.
 */
HAL_StatusTypeDef BQ27441_Update(BQ27441_Device_t *dev)
{
    uint16_t raw_voltage;
    uint16_t raw_temp;
    uint16_t raw_current;
    uint16_t raw_soc;
    HAL_StatusTypeDef ret;

    if (dev == NULL)
    {
        return HAL_ERROR;
    }

    ret = BQ27441_ReadWord(dev, BQ27441_CMD_VOLTAGE, &raw_voltage);
    if (ret != HAL_OK)
    {
        return ret;
    }
    dev->voltage = (float)raw_voltage / 1000.0f;

    ret = BQ27441_ReadWord(dev, BQ27441_CMD_TEMP, &raw_temp);
    if (ret != HAL_OK)
    {
        return ret;
    }
    dev->temperature = ((float)raw_temp / 10.0f) - 273.15f;

    ret = BQ27441_ReadWord(dev, BQ27441_CMD_AVG_CURRENT, &raw_current);
    if (ret != HAL_OK)
    {
        return ret;
    }
    dev->current = (float)(int16_t)raw_current / 1000.0f;

    ret = BQ27441_ReadWord(dev, BQ27441_CMD_SOC, &raw_soc);
    if (ret != HAL_OK)
    {
        return ret;
    }
    dev->soc = (uint8_t)raw_soc;

    if (dev->current > CURRENT_THRESHOLD_MA)
    {
        dev->charge_status = BQ27441_CHARGE_STATUS_CHARGING;
    }
    else if (dev->current < -CURRENT_THRESHOLD_MA)
    {
        dev->charge_status = BQ27441_CHARGE_STATUS_DISCHARGING;
    }
    else
    {
        dev->charge_status = BQ27441_CHARGE_STATUS_SLEEP;
    }

    return HAL_OK;
}
