/**
  ******************************************************************************
  * @file    thermal.h
  * @author  STEdgeAI
  * @date    2026-07-01 21:57:53
  * @brief   Minimal description of the generated c-implemention of the network
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  ******************************************************************************
  */
#ifndef LL_ATON_THERMAL_H
#define LL_ATON_THERMAL_H

/******************************************************************************/
#define LL_ATON_THERMAL_C_MODEL_NAME        "thermal"
#define LL_ATON_THERMAL_ORIGIN_MODEL_NAME   "best_model_int8_cubeai_padded"

/************************** USER ALLOCATED IOs ********************************/
// No user allocated inputs
// No user allocated outputs

/************************** INPUTS ********************************************/
#define LL_ATON_THERMAL_IN_NUM        (1)    // Total number of input buffers
// Input buffer 1 -- Input_0_out_0
#define LL_ATON_THERMAL_IN_1_ALIGNMENT   (32)
#define LL_ATON_THERMAL_IN_1_SIZE_BYTES  (19200)

/************************** OUTPUTS *******************************************/
#define LL_ATON_THERMAL_OUT_NUM        (1)    // Total number of output buffers
// Output buffer 1 -- Transpose_27_out_0
#define LL_ATON_THERMAL_OUT_1_ALIGNMENT   (32)
#define LL_ATON_THERMAL_OUT_1_SIZE_BYTES  (2400)

#endif /* LL_ATON_THERMAL_H */
