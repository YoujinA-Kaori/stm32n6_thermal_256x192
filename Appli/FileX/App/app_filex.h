
/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_filex.h
  * @author  MCD Application Team
  * @brief   FileX applicative header file
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
#ifndef __APP_FILEX_H__
#define __APP_FILEX_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include "fx_api.h"
#include "fx_stm32_sd_driver.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */
#define CFG_APP_FILEX_SNAPSHOT_NAME_LEN  16U

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
UINT MX_FileX_Init(VOID *memory_ptr);

/* USER CODE BEGIN EFP */
uint8_t app_filex_is_ready(void);
UINT app_filex_get_last_status(void);
UINT app_filex_prepare(void);
UINT app_filex_snapshot_save_rgb565(const uint16_t *frame_buffer_ptr,
                                    uint16_t width,
                                    uint16_t height,
                                    CHAR *file_name_ptr,
                                    uint32_t file_name_size);
UINT app_filex_snapshot_get_list(CHAR *file_name_buffer_ptr,
                                 uint32_t max_entries,
                                 uint32_t name_stride,
                                 uint32_t *entry_count_ptr);
UINT app_filex_snapshot_get_latest(CHAR *file_name_ptr,
                                   uint32_t file_name_size,
                                   uint32_t *entry_count_ptr,
                                   uint32_t *snapshot_index_ptr);
UINT app_filex_snapshot_get_adjacent(uint32_t current_snapshot_index,
                                     uint8_t next,
                                     CHAR *file_name_ptr,
                                     uint32_t file_name_size,
                                     uint32_t *entry_count_ptr,
                                     uint32_t *entry_rank_ptr,
                                     uint32_t *snapshot_index_ptr);UINT app_filex_snapshot_load_rgb565(const CHAR *file_name_ptr,
                                    uint16_t *frame_buffer_ptr,
                                    uint32_t frame_buffer_pixels,
                                    uint16_t *width_ptr,
                                    uint16_t *height_ptr);
UINT app_filex_snapshot_delete(const CHAR *file_name_ptr);
const char *app_filex_status_to_string(UINT status);

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
/* Main thread Name */
#ifndef FX_APP_THREAD_NAME
  #define FX_APP_THREAD_NAME "FileX app thread"
#endif

/* Main thread time slice */
#ifndef FX_APP_THREAD_TIME_SLICE
  #define FX_APP_THREAD_TIME_SLICE TX_NO_TIME_SLICE
#endif

/* Main thread auto start */
#ifndef FX_APP_THREAD_AUTO_START
  #define FX_APP_THREAD_AUTO_START TX_AUTO_START
#endif

/* Main thread preemption threshold */
#ifndef FX_APP_PREEMPTION_THRESHOLD
  #define FX_APP_PREEMPTION_THRESHOLD FX_APP_THREAD_PRIO
#endif

/* fx sd volume name */
#ifndef FX_SD_VOLUME_NAME
  #define FX_SD_VOLUME_NAME "STM32_SDIO_DISK"
#endif
/* fx sd number of FATs */
#ifndef FX_SD_NUMBER_OF_FATS
  #define FX_SD_NUMBER_OF_FATS                1
#endif

/* fx sd Hidden sectors */
#ifndef FX_SD_HIDDEN_SECTORS
  #define FX_SD_HIDDEN_SECTORS               0
#endif

/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
#ifdef __cplusplus
}
#endif
#endif /* __APP_FILEX_H__ */
