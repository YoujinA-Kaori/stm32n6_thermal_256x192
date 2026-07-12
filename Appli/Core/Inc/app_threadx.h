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
  uint8_t visible_alpha;
} app_camera_alignment_t;

#define CFG_APP_THERMAL_AI_SNAPSHOT_MAX_BOXES  8U
#define CFG_APP_THERMAL_AI_SNAPSHOT_LABEL_LEN  32U

typedef struct
{
  uint8_t valid;
  uint16_t x;
  uint16_t y;
  uint16_t width;
  uint16_t height;
  uint32_t border_color_rgb888;
  char label_text[CFG_APP_THERMAL_AI_SNAPSHOT_LABEL_LEN];
} app_thermal_ai_snapshot_box_t;

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
uint8_t app_uart_is_file_mode_active(void);
void app_thermal_ai_set_enabled(uint8_t enable);
uint8_t app_thermal_ai_is_enabled(void);
void app_thermal_ai_set_preview_pseudo_mode(uint8_t pseudo_mode);
uint8_t app_thermal_ai_get_preview_pseudo_mode(void);
uint32_t app_thermal_ai_snapshot_collect_boxes(uint8_t fullscreen,
                                               app_thermal_ai_snapshot_box_t *box_array,
                                               uint32_t max_boxes);
void app_imx219_on_frame_event(void);
void app_camera_view_set_mode(app_camera_view_mode_t mode);
app_camera_view_mode_t app_camera_view_get_mode(void);
void app_camera_alignment_get(app_camera_alignment_t *alignment);
void app_camera_alignment_adjust(int16_t delta_x,
                                 int16_t delta_y,
                                 int16_t delta_scale_permille,
                                 int16_t delta_alpha);
void app_camera_alignment_reset(void);

/* USER CODE END EFP */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

#ifdef __cplusplus
}
#endif
#endif /* __APP_THREADX_H */
