/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_threadx.h
  * @author  MCD Application Team
  * @brief   ThreadX applicative header file
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
#ifndef __APP_THREADX_H
#define __APP_THREADX_H
#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include "tx_api.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
typedef enum
{
  APP_UART_FILE_HOLD_NONE = 0x00U,
  APP_UART_FILE_HOLD_GUI  = 0x01U,
  APP_UART_FILE_HOLD_WEB  = 0x02U
} app_uart_file_hold_t;

typedef enum
{
  APP_CAMERA_VIEW_THERMAL = 0U,
  APP_CAMERA_VIEW_VISIBLE,
  APP_CAMERA_VIEW_FUSION
} app_camera_view_mode_t;

typedef struct
{
  int16_t offset_x;
  int16_t offset_y;
  uint16_t scale_permille;
  int8_t rotation_degrees;
  uint8_t transform_flags;
  uint8_t visible_alpha;
} app_camera_alignment_t;

#define APP_CAMERA_ALIGNMENT_MIRROR 0x01U
#define APP_CAMERA_ALIGNMENT_FLIP   0x02U

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Private defines -----------------------------------------------------------*/

/* USER CODE BEGIN PD */
/* Keep CACHEAXI out of the thermal-AI path for now.
   The current priority is stable board-side detection rather than peak AI throughput. */
#define CFG_THERMAL_AI_DIAG_DISABLE_NPU_CACHE 1U

/* USER CODE END PD */

/* Main thread defines -------------------------------------------------------*/
/* USER CODE BEGIN MTD */

/* USER CODE END MTD */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
UINT App_ThreadX_Init(VOID *memory_ptr);
void MX_ThreadX_Init(void);

/* USER CODE BEGIN EFP */
void app_uart_request_file_mode(uint32_t hold_mask);
void app_uart_release_file_mode(uint32_t hold_mask);
void app_thermal_ai_set_enabled(uint8_t enable);
uint8_t app_thermal_ai_is_enabled(void);
void app_imx219_on_frame_event(void);
void app_imx219_on_dcmipp_error(uint32_t error_code);
void app_imx219_on_csi_line_error(uint32_t data_lane, uint32_t error_code);
void app_camera_view_set_mode(app_camera_view_mode_t mode);
app_camera_view_mode_t app_camera_view_get_mode(void);
void app_camera_alignment_get(app_camera_alignment_t *alignment);
void app_camera_alignment_adjust(int16_t delta_x,
                                  int16_t delta_y,
                                  int16_t delta_scale_permille,
                                  int8_t delta_rotation_degrees,
                                  int16_t delta_alpha);
void app_camera_alignment_toggle(uint8_t transform_flag);
void app_camera_alignment_reset(void);

/* USER CODE END EFP */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

#ifdef __cplusplus
}
#endif
#endif /* __APP_THREADX_H */
