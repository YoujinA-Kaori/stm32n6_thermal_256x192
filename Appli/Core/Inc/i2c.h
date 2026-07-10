/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    i2c.h
  * @brief   This file contains all the function prototypes for
  *          the i2c.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __I2C_H__
#define __I2C_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */
#include "tx_api.h"

/* USER CODE END Includes */

extern I2C_HandleTypeDef hi2c4;

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_I2C4_Init(void);

/* USER CODE BEGIN Prototypes */
/**
 * @brief Initialize the shared I2C4 bus mutex used by devices on the hardware bus.
 * @param None
 * @return UINT `TX_SUCCESS` when the mutex is ready.
 */
UINT app_i2c4_bus_mutex_init(void);

/**
 * @brief Acquire the shared I2C4 bus mutex and block until the bus becomes available.
 * @param None
 * @return UINT `TX_SUCCESS` when the caller owns the bus.
 */
UINT app_i2c4_bus_lock(void);

/**
 * @brief Release the shared I2C4 bus mutex when it is held.
 * @param None
 * @return None
 */
void app_i2c4_bus_unlock(void);

/**
 * @brief Recover the shared I2C4 bus while the caller already owns the bus mutex.
 * @param None
 * @return None
 * @note The caller must hold the shared I2C4 bus mutex before calling this API.
 */
void app_i2c4_bus_recover_locked(void);

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __I2C_H__ */

