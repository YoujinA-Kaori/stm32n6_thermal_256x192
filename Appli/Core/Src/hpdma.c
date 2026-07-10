/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    hpdma.c
  * @brief   This file provides code for the configuration
  *          of the HPDMA instances.
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
#include "hpdma.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

DMA_HandleTypeDef handle_HPDMA1_Channel15;

/* HPDMA1 init function */
void MX_HPDMA1_Init(void)
{

  /* USER CODE BEGIN HPDMA1_Init 0 */

  /* USER CODE END HPDMA1_Init 0 */

  DMA_IsolationConfigTypeDef IsolationConfiginput = {0};

  /* USER CODE BEGIN HPDMA1_Init 1 */

  /* USER CODE END HPDMA1_Init 1 */
  handle_HPDMA1_Channel15.Instance = HPDMA1_Channel15;
  handle_HPDMA1_Channel15.InitLinkedList.Priority = DMA_HIGH_PRIORITY;
  handle_HPDMA1_Channel15.InitLinkedList.LinkStepMode = DMA_LSM_FULL_EXECUTION;
  handle_HPDMA1_Channel15.InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT0;
  handle_HPDMA1_Channel15.InitLinkedList.TransferEventMode = DMA_TCEM_LAST_LL_ITEM_TRANSFER;
  handle_HPDMA1_Channel15.InitLinkedList.LinkedListMode = DMA_LINKEDLIST_CIRCULAR;
  if (HAL_DMAEx_List_Init(&handle_HPDMA1_Channel15) != HAL_OK)
  {
    Error_Handler();
  }
  IsolationConfiginput.CidFiltering = DMA_ISOLATION_OFF;
  IsolationConfiginput.StaticCid = DMA_CHANNEL_STATIC_CID_0;
  if (HAL_DMA_SetIsolationAttributes(&handle_HPDMA1_Channel15, &IsolationConfiginput) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN HPDMA1_Init 2 */

  /* USER CODE END HPDMA1_Init 2 */

}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

