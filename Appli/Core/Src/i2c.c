/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    i2c.c
  * @brief   This file provides code for the configuration
  *          of the I2C instances.
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
/* Includes ------------------------------------------------------------------*/
#include "i2c.h"

/* USER CODE BEGIN 0 */
static TX_MUTEX g_app_i2c4_bus_mutex;
static volatile uint8_t g_app_i2c4_bus_mutex_created = 0U;

#define APP_I2C4_RECOVERY_PULSE_COUNT    9U
#define APP_I2C4_RECOVERY_DELAY_MS       1U

/* USER CODE END 0 */

I2C_HandleTypeDef hi2c4;

/* I2C4 init function */
void MX_I2C4_Init(void)
{

  /* USER CODE BEGIN I2C4_Init 0 */

  /* USER CODE END I2C4_Init 0 */

  /* USER CODE BEGIN I2C4_Init 1 */

  /* USER CODE END I2C4_Init 1 */
  hi2c4.Instance = I2C4;
  hi2c4.Init.Timing = 0x10707DBC;
  hi2c4.Init.OwnAddress1 = 0;
  hi2c4.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c4.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c4.Init.OwnAddress2 = 0;
  hi2c4.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c4.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c4.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c4) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c4, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c4, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C4_Init 2 */

  /* USER CODE END I2C4_Init 2 */

}

void HAL_I2C_MspInit(I2C_HandleTypeDef* i2cHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  if(i2cHandle->Instance==I2C4)
  {
  /* USER CODE BEGIN I2C4_MspInit 0 */

  /* USER CODE END I2C4_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_I2C4;
    PeriphClkInitStruct.I2c4ClockSelection = RCC_I2C4CLKSOURCE_CLKP;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_RCC_GPIOE_CLK_ENABLE();
    /**I2C4 GPIO Configuration
    PE13     ------> I2C4_SCL
    PE14     ------> I2C4_SDA
    */
    GPIO_InitStruct.Pin = GPIO_PIN_13|GPIO_PIN_14;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C4;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

    /* I2C4 clock enable */
    __HAL_RCC_I2C4_CLK_ENABLE();
  /* USER CODE BEGIN I2C4_MspInit 1 */

  /* USER CODE END I2C4_MspInit 1 */
  }
}

void HAL_I2C_MspDeInit(I2C_HandleTypeDef* i2cHandle)
{

  if(i2cHandle->Instance==I2C4)
  {
  /* USER CODE BEGIN I2C4_MspDeInit 0 */

  /* USER CODE END I2C4_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_I2C4_CLK_DISABLE();

    /**I2C4 GPIO Configuration
    PE13     ------> I2C4_SCL
    PE14     ------> I2C4_SDA
    */
    HAL_GPIO_DeInit(GPIOE, GPIO_PIN_13);

    HAL_GPIO_DeInit(GPIOE, GPIO_PIN_14);

  /* USER CODE BEGIN I2C4_MspDeInit 1 */

  /* USER CODE END I2C4_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
/**
  * @brief  Initialize the shared I2C4 bus mutex.
  * @param  None
  * @retval UINT `TX_SUCCESS` when the mutex is ready.
  */
UINT app_i2c4_bus_mutex_init(void)
{
  if (g_app_i2c4_bus_mutex_created == 0U)
  {
    if (tx_mutex_create(&g_app_i2c4_bus_mutex, "app_i2c4_bus_mutex", TX_INHERIT) != TX_SUCCESS)
    {
      return TX_MUTEX_ERROR;
    }

    g_app_i2c4_bus_mutex_created = 1U;
  }

  return TX_SUCCESS;
}

/**
  * @brief  Acquire the shared I2C4 bus mutex and wait forever until it becomes available.
  * @param  None
  * @retval UINT `TX_SUCCESS` when the caller owns the bus.
  */
UINT app_i2c4_bus_lock(void)
{
  UINT status;

  status = app_i2c4_bus_mutex_init();
  if (status != TX_SUCCESS)
  {
    return status;
  }

  return tx_mutex_get(&g_app_i2c4_bus_mutex, TX_WAIT_FOREVER);
}

/**
  * @brief  Release the shared I2C4 bus mutex when it is held.
  * @param  None
  * @retval None
  */
void app_i2c4_bus_unlock(void)
{
  if (g_app_i2c4_bus_mutex_created != 0U)
  {
    (void)tx_mutex_put(&g_app_i2c4_bus_mutex);
  }
}

/**
  * @brief  Recover the shared I2C4 bus while the caller already owns the bus mutex.
  * @param  None
  * @retval None
  */
void app_i2c4_bus_recover_locked(void)
{
  GPIO_InitTypeDef gpio_init = {0};
  uint32_t pulse_index;

  (void)HAL_I2C_DeInit(&hi2c4);

  __HAL_RCC_GPIOE_CLK_ENABLE();

  gpio_init.Pin = GPIO_PIN_13 | GPIO_PIN_14;
  gpio_init.Mode = GPIO_MODE_OUTPUT_OD;
  gpio_init.Pull = GPIO_NOPULL;
  gpio_init.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOE, &gpio_init);

  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13 | GPIO_PIN_14, GPIO_PIN_SET);
  HAL_Delay(APP_I2C4_RECOVERY_DELAY_MS);

  for (pulse_index = 0U; pulse_index < APP_I2C4_RECOVERY_PULSE_COUNT; ++pulse_index)
  {
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13, GPIO_PIN_RESET);
    HAL_Delay(APP_I2C4_RECOVERY_DELAY_MS);
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13, GPIO_PIN_SET);
    HAL_Delay(APP_I2C4_RECOVERY_DELAY_MS);
  }

  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_14, GPIO_PIN_RESET);
  HAL_Delay(APP_I2C4_RECOVERY_DELAY_MS);
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13, GPIO_PIN_SET);
  HAL_Delay(APP_I2C4_RECOVERY_DELAY_MS);
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_14, GPIO_PIN_SET);
  HAL_Delay(APP_I2C4_RECOVERY_DELAY_MS);

  MX_I2C4_Init();
}

/* USER CODE END 1 */

