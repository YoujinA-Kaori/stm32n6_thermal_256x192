/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_filex.c
  * @author  MCD Application Team
  * @brief   FileX thermal snapshot storage service.
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
#include "app_filex.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tx_api.h"
#include "sdmmc.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct
{
  uint32_t snapshot_index;
  CHAR file_name[CFG_APP_FILEX_SNAPSHOT_NAME_LEN];
} app_filex_snapshot_entry_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* Main thread stack size */
#define FX_APP_THREAD_STACK_SIZE                 1024U
/* Main thread priority */
#define FX_APP_THREAD_PRIO                       10U
/* USER CODE BEGIN PD */
#define CFG_APP_FILEX_SNAPSHOT_DIR_NAME         "THERMAL"
#define CFG_APP_FILEX_VOLUME_LABEL              "THERMAL"
#define CFG_APP_FILEX_BMP_FILE_HEADER_SIZE      14U
#define CFG_APP_FILEX_BMP_INFO_HEADER_SIZE      40U
#define CFG_APP_FILEX_BMP_MASK_SIZE             12U
#define CFG_APP_FILEX_BMP_HEADER_SIZE           (CFG_APP_FILEX_BMP_FILE_HEADER_SIZE + CFG_APP_FILEX_BMP_INFO_HEADER_SIZE + CFG_APP_FILEX_BMP_MASK_SIZE)
#define CFG_APP_FILEX_MAX_LISTED_SNAPSHOTS      32U
#define CFG_APP_FILEX_ENUM_NAME_LEN             64U
#define CFG_APP_FILEX_DEFAULT_DIR_ENTRIES       512U
#define CFG_APP_FILEX_BMP_COMPRESSION_BITFIELDS 3U
#define CFG_APP_FILEX_BMP_PLANES                1U
#define CFG_APP_FILEX_BMP_BPP_RGB565            16U
#define CFG_APP_FILEX_BMP_PPM                   2835U
#define CFG_APP_FILEX_MAX_WIDTH                 640U
#define CFG_APP_FILEX_MAX_HEIGHT                480U
#define CFG_APP_FILEX_PREPARE_RETRIES           3U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
TX_MUTEX g_app_filex_mutex;

/* Buffer for FileX FX_MEDIA sector cache. */
ALIGN_32BYTES(uint32_t fx_sd_media_memory[FX_STM32_SD_DEFAULT_SECTOR_SIZE / sizeof(uint32_t)]);
/* Define FileX global data structures.  */
FX_MEDIA sdio_disk;

/* USER CODE BEGIN PV */
static volatile uint8_t g_app_filex_ready = 0U;
static volatile UINT g_app_filex_last_status = FX_NOT_OPEN;
/* USER CODE END PV */

/* USER CODE BEGIN PFP */
/**
  * @brief  Mount the SD media and ensure the thermal snapshot directory exists.
  * @retval FileX status code.
  */
static UINT app_filex_mount_media_locked(void);

/**
  * @brief  Format the SD media using a compact FAT layout.
  * @retval FileX status code.
  */
static UINT app_filex_format_media_locked(void);

/**
  * @brief  Ensure the FileX media is mounted before an operation.
  * @retval FileX status code.
  */
static UINT app_filex_ensure_ready_locked(void);

/**
  * @brief  Collect snapshot file entries from the THERMAL directory.
  * @param  entry_list_ptr Snapshot entry output array.
  * @param  entry_capacity Number of entries available in @p entry_list_ptr.
  * @param  entry_count_ptr Returns the number of collected entries.
  * @param  max_index_ptr Returns the highest snapshot index found.
  * @retval FileX status code.
  */
static UINT app_filex_collect_entries_locked(app_filex_snapshot_entry_t *entry_list_ptr,
                                             uint32_t entry_capacity,
                                             uint32_t *entry_count_ptr,
                                             uint32_t *max_index_ptr);

/**
  * @brief  Insert one snapshot entry into an ascending index list.
  * @param  entry_list_ptr Snapshot entry array.
  * @param  entry_count_ptr Current entry count, updated in place.
  * @param  entry_capacity Array capacity.
  * @param  snapshot_index Parsed numeric snapshot index.
  * @param  file_name_ptr Snapshot file name.
  * @retval None.
  */
static void app_filex_insert_entry_sorted(app_filex_snapshot_entry_t *entry_list_ptr,
                                          uint32_t *entry_count_ptr,
                                          uint32_t entry_capacity,
                                          uint32_t snapshot_index,
                                          const CHAR *file_name_ptr);

/**
  * @brief  Parse the numeric index from a snapshot file name.
  * @param  file_name_ptr Snapshot file name in THMxxxxx.BMP format.
  * @retval Parsed index, or 0 when the name is invalid.
  */
static uint32_t app_filex_parse_snapshot_index(const CHAR *file_name_ptr);

/**
  * @brief  Check whether a directory entry matches the THMxxxxx.BMP pattern.
  * @param  file_name_ptr Snapshot file name.
  * @retval 1 when the name matches, otherwise 0.
  */
static uint8_t app_filex_is_snapshot_name(const CHAR *file_name_ptr);

/**
  * @brief  Build one THMxxxxx.BMP file name from a numeric index.
  * @param  snapshot_index Sequential snapshot index.
  * @param  file_name_ptr Output file name buffer.
  * @param  file_name_size Size of @p file_name_ptr in bytes.
  * @retval FileX status code.
  */
static UINT app_filex_build_snapshot_name(uint32_t snapshot_index, CHAR *file_name_ptr, uint32_t file_name_size);

/**
  * @brief  Serialize one top-down RGB565 BMP header.
  * @param  header_buffer_ptr Output buffer of CFG_APP_FILEX_BMP_HEADER_SIZE bytes.
  * @param  width Snapshot width in pixels.
  * @param  height Snapshot height in pixels.
  * @retval None.
  */
static void app_filex_build_bmp_header(uint8_t *header_buffer_ptr, uint16_t width, uint16_t height);

/**
  * @brief  Write one 16-bit little-endian value into a byte buffer.
  * @param  dest_ptr Destination byte pointer.
  * @param  value Value to serialize.
  * @retval None.
  */
static void app_filex_write_le16(uint8_t *dest_ptr, uint16_t value);

/**
  * @brief  Write one 32-bit little-endian value into a byte buffer.
  * @param  dest_ptr Destination byte pointer.
  * @param  value Value to serialize.
  * @retval None.
  */
static void app_filex_write_le32(uint8_t *dest_ptr, uint32_t value);

/**
  * @brief  Read one 16-bit little-endian value from a byte buffer.
  * @param  src_ptr Source byte pointer.
  * @retval Parsed 16-bit value.
  */
static uint16_t app_filex_read_le16(const uint8_t *src_ptr);

/**
  * @brief  Read one 32-bit little-endian value from a byte buffer.
  * @param  src_ptr Source byte pointer.
  * @retval Parsed 32-bit value.
  */
static uint32_t app_filex_read_le32(const uint8_t *src_ptr);

/**
  * @brief  Read one signed 32-bit little-endian value from a byte buffer.
  * @param  src_ptr Source byte pointer.
  * @retval Parsed signed 32-bit value.
  */
static int32_t app_filex_read_le32s(const uint8_t *src_ptr);

/**
  * @brief  Convert a FileX status code to a short UI-friendly string.
  * @param  status FileX status code.
  * @retval const char* Static status text.
  */
static const char *app_filex_status_to_string_internal(UINT status);
/* USER CODE END PFP */

/**
  * @brief  Application FileX Initialization.
  * @param  memory_ptr Memory pool pointer.
  * @retval int FileX initialization status.
  */
UINT MX_FileX_Init(VOID *memory_ptr)
{
  UINT ret = FX_SUCCESS;
  TX_PARAMETER_NOT_USED(memory_ptr);

  /* USER CODE BEGIN MX_FileX_MEM_POOL */

  /* USER CODE END MX_FileX_MEM_POOL */

  /* USER CODE BEGIN 0 */

  /* USER CODE END 0 */

  ret = tx_mutex_create(&g_app_filex_mutex, "app_filex_mutex", TX_INHERIT);
  if (ret != TX_SUCCESS)
  {
    return ret;
  }

  /* USER CODE BEGIN MX_FileX_Init */
  g_app_filex_ready = 0U;
  g_app_filex_last_status = FX_NOT_OPEN;
  /* USER CODE END MX_FileX_Init */

  /* Initialize FileX.  */
  fx_system_initialize();

  /* USER CODE BEGIN MX_FileX_Init 1*/

  /* USER CODE END MX_FileX_Init 1*/

  return ret;
}

/* USER CODE BEGIN 1 */
/**
  * @brief  Report whether the snapshot storage media is ready.
  * @retval uint8_t 1 when ready, otherwise 0.
  */
uint8_t app_filex_is_ready(void)
{
  return g_app_filex_ready;
}

/**
  * @brief  Get the last storage status code observed by the service.
  * @retval UINT FileX status code.
  */
UINT app_filex_get_last_status(void)
{
  return g_app_filex_last_status;
}

/**
  * @brief  Ensure the FileX media is mounted and ready for later use.
  * @retval FileX status code.
  */
UINT app_filex_prepare(void)
{
  UINT status;
  uint32_t retry_index;

  for (retry_index = 0U; retry_index < CFG_APP_FILEX_PREPARE_RETRIES; retry_index++)
  {
    status = tx_mutex_get(&g_app_filex_mutex, TX_WAIT_FOREVER);
    if (status != TX_SUCCESS)
    {
      return status;
    }

    status = app_filex_ensure_ready_locked();
    g_app_filex_last_status = status;
    if (status == FX_SUCCESS)
    {
      g_app_filex_ready = 1U;
      (void)tx_mutex_put(&g_app_filex_mutex);
      return FX_SUCCESS;
    }

    g_app_filex_ready = 0U;
    (void)tx_mutex_put(&g_app_filex_mutex);
    tx_thread_sleep((TX_TIMER_TICKS_PER_SECOND >= 10U) ? (TX_TIMER_TICKS_PER_SECOND / 10U) : 1U);
  }

  return status;
}

/**
  * @brief  Save one RGB565 top-down thermal snapshot as a BMP file.
  * @param  frame_buffer_ptr Pointer to the RGB565 frame buffer.
  * @param  width Frame width in pixels.
  * @param  height Frame height in pixels.
  * @param  file_name_ptr Optional output buffer for the generated file name.
  * @param  file_name_size Size of @p file_name_ptr in bytes.
  * @retval FileX status code.
  */
UINT app_filex_snapshot_save_rgb565(const uint16_t *frame_buffer_ptr,
                                    uint16_t width,
                                    uint16_t height,
                                    CHAR *file_name_ptr,
                                    uint32_t file_name_size)
{
  UINT status;
  FX_FILE file_handle;
  app_filex_snapshot_entry_t entry_list[CFG_APP_FILEX_MAX_LISTED_SNAPSHOTS];
  uint32_t entry_count = 0U;
  uint32_t max_index = 0U;
  CHAR generated_name[CFG_APP_FILEX_SNAPSHOT_NAME_LEN];
  uint8_t header_buffer[CFG_APP_FILEX_BMP_HEADER_SIZE];
  uint8_t file_opened = 0U;

  if ((frame_buffer_ptr == NULL) || (width == 0U) || (height == 0U))
  {
    return FX_PTR_ERROR;
  }

  if ((width > CFG_APP_FILEX_MAX_WIDTH) || (height > CFG_APP_FILEX_MAX_HEIGHT))
  {
    return FX_INVALID_PATH;
  }

  status = tx_mutex_get(&g_app_filex_mutex, TX_WAIT_FOREVER);
  if (status != TX_SUCCESS)
  {
    return status;
  }

  status = app_filex_ensure_ready_locked();
  if (status == FX_SUCCESS)
  {
    status = app_filex_collect_entries_locked(entry_list,
                                              CFG_APP_FILEX_MAX_LISTED_SNAPSHOTS,
                                              &entry_count,
                                              &max_index);
  }
  if (status == FX_SUCCESS)
  {
    status = app_filex_build_snapshot_name(max_index + 1U, generated_name, sizeof(generated_name));
  }
  if (status == FX_SUCCESS)
  {
    status = fx_directory_default_set(&sdio_disk, "/" CFG_APP_FILEX_SNAPSHOT_DIR_NAME);
  }
  if (status == FX_SUCCESS)
  {
    (void)fx_file_create(&sdio_disk, generated_name);
    status = fx_file_open(&sdio_disk, &file_handle, generated_name, FX_OPEN_FOR_WRITE);
    if (status == FX_SUCCESS)
    {
      file_opened = 1U;
    }
  }
  if (status == FX_SUCCESS)
  {
    status = fx_file_truncate(&file_handle, 0U);
  }
  if (status == FX_SUCCESS)
  {
    status = fx_file_seek(&file_handle, 0U);
  }
  if (status == FX_SUCCESS)
  {
    app_filex_build_bmp_header(header_buffer, width, height);
    status = fx_file_write(&file_handle, header_buffer, sizeof(header_buffer));
  }
  if (status == FX_SUCCESS)
  {
    status = fx_file_write(&file_handle,
                           (VOID *)frame_buffer_ptr,
                           (ULONG)((uint32_t)width * (uint32_t)height * sizeof(uint16_t)));
  }
  if (status == FX_SUCCESS)
  {
    status = fx_file_close(&file_handle);
    if (status == FX_SUCCESS)
    {
      status = fx_media_flush(&sdio_disk);
    }
  }
  else
  {
    if (file_opened != 0U)
    {
      (void)fx_file_close(&file_handle);
    }
  }

  (void)fx_directory_default_set(&sdio_disk, "/");

  if ((status == FX_SUCCESS) && (file_name_ptr != NULL) && (file_name_size > 0U))
  {
    (void)snprintf(file_name_ptr, (size_t)file_name_size, "%s", generated_name);
  }

  g_app_filex_last_status = status;
  g_app_filex_ready = (status == FX_SUCCESS) ? 1U : g_app_filex_ready;
  (void)tx_mutex_put(&g_app_filex_mutex);

  return status;
}

/**
  * @brief  Enumerate thermal snapshot BMP files.
  * @param  file_name_buffer_ptr Output file-name array buffer.
  * @param  max_entries Maximum number of names to output.
  * @param  name_stride Distance in bytes between adjacent names.
  * @param  entry_count_ptr Returns the number of valid entries.
  * @retval FileX status code.
  */
UINT app_filex_snapshot_get_list(CHAR *file_name_buffer_ptr,
                                 uint32_t max_entries,
                                 uint32_t name_stride,
                                 uint32_t *entry_count_ptr)
{
  UINT status;
  app_filex_snapshot_entry_t entry_list[CFG_APP_FILEX_MAX_LISTED_SNAPSHOTS];
  uint32_t entry_count = 0U;
  uint32_t max_index = 0U;
  uint32_t entry_index;

  if ((file_name_buffer_ptr == NULL) || (entry_count_ptr == NULL) || (name_stride < CFG_APP_FILEX_SNAPSHOT_NAME_LEN))
  {
    return FX_PTR_ERROR;
  }

  if (max_entries > CFG_APP_FILEX_MAX_LISTED_SNAPSHOTS)
  {
    max_entries = CFG_APP_FILEX_MAX_LISTED_SNAPSHOTS;
  }

  status = tx_mutex_get(&g_app_filex_mutex, TX_WAIT_FOREVER);
  if (status != TX_SUCCESS)
  {
    return status;
  }

  status = app_filex_ensure_ready_locked();
  if (status == FX_SUCCESS)
  {
    status = app_filex_collect_entries_locked(entry_list, max_entries, &entry_count, &max_index);
  }
  if (status == FX_SUCCESS)
  {
    for (entry_index = 0U; entry_index < entry_count; entry_index++)
    {
      (void)snprintf((char *)(file_name_buffer_ptr + (entry_index * name_stride)),
                     (size_t)name_stride,
                     "%s",
                     entry_list[entry_index].file_name);
    }
    *entry_count_ptr = entry_count;
  }
  else
  {
    *entry_count_ptr = 0U;
  }

  g_app_filex_last_status = status;
  (void)tx_mutex_put(&g_app_filex_mutex);

  (void)max_index;
  return status;
}

/**
  * @brief  Load one saved RGB565 BMP snapshot into memory.
  * @param  file_name_ptr Snapshot file name in THMxxxxx.BMP format.
  * @param  frame_buffer_ptr Output RGB565 frame buffer.
  * @param  frame_buffer_pixels Number of pixels available in @p frame_buffer_ptr.
  * @param  width_ptr Returns the decoded width.
  * @param  height_ptr Returns the decoded height.
  * @retval FileX status code.
  */
UINT app_filex_snapshot_get_latest(CHAR *file_name_ptr,
                                   uint32_t file_name_size,
                                   uint32_t *entry_count_ptr,
                                   uint32_t *snapshot_index_ptr)
{
  UINT status;
  UINT attributes = 0U;
  ULONG size = 0UL;
  UINT year = 0U;
  UINT month = 0U;
  UINT day = 0U;
  UINT hour = 0U;
  UINT minute = 0U;
  UINT second = 0U;
  CHAR entry_name[CFG_APP_FILEX_ENUM_NAME_LEN];
  uint32_t entry_count = 0U;
  uint32_t max_index = 0U;

  if ((file_name_ptr == NULL) || (file_name_size < CFG_APP_FILEX_SNAPSHOT_NAME_LEN) ||
      (entry_count_ptr == NULL) || (snapshot_index_ptr == NULL))
  {
    return FX_PTR_ERROR;
  }

  *entry_count_ptr = 0U;
  *snapshot_index_ptr = 0U;
  file_name_ptr[0] = '\0';

  status = tx_mutex_get(&g_app_filex_mutex, TX_WAIT_FOREVER);
  if (status != TX_SUCCESS)
  {
    return status;
  }

  status = app_filex_ensure_ready_locked();
  if (status == FX_SUCCESS)
  {
    status = fx_directory_default_set(&sdio_disk, "/" CFG_APP_FILEX_SNAPSHOT_DIR_NAME);
  }
  if (status == FX_SUCCESS)
  {
    status = fx_directory_first_full_entry_find(&sdio_disk,
                                                entry_name,
                                                &attributes,
                                                &size,
                                                &year,
                                                &month,
                                                &day,
                                                &hour,
                                                &minute,
                                                &second);
  }
  while (status == FX_SUCCESS)
  {
    if (((attributes & FX_DIRECTORY) == 0U) && (app_filex_is_snapshot_name(entry_name) != 0U))
    {
      uint32_t snapshot_index = app_filex_parse_snapshot_index(entry_name);
      if (snapshot_index > 0U)
      {
        entry_count++;
        if (snapshot_index > max_index)
        {
          max_index = snapshot_index;
        }
      }
    }

    status = fx_directory_next_full_entry_find(&sdio_disk,
                                               entry_name,
                                               &attributes,
                                               &size,
                                               &year,
                                               &month,
                                               &day,
                                               &hour,
                                               &minute,
                                               &second);
  }

  (void)fx_directory_default_set(&sdio_disk, "/");
  if (status == FX_NO_MORE_ENTRIES)
  {
    status = FX_SUCCESS;
  }
  if ((status == FX_SUCCESS) && (entry_count == 0U))
  {
    status = FX_NO_MORE_ENTRIES;
  }
  if (status == FX_SUCCESS)
  {
    status = app_filex_build_snapshot_name(max_index, file_name_ptr, file_name_size);
  }
  if (status == FX_SUCCESS)
  {
    *entry_count_ptr = entry_count;
    *snapshot_index_ptr = max_index;
  }

  g_app_filex_last_status = status;
  (void)tx_mutex_put(&g_app_filex_mutex);
  return status;
}

UINT app_filex_snapshot_get_adjacent(uint32_t current_snapshot_index,
                                     uint8_t next,
                                     CHAR *file_name_ptr,
                                     uint32_t file_name_size,
                                     uint32_t *entry_count_ptr,
                                     uint32_t *entry_rank_ptr,
                                     uint32_t *snapshot_index_ptr)
{
  UINT status;
  UINT attributes = 0U;
  ULONG size = 0UL;
  UINT year = 0U;
  UINT month = 0U;
  UINT day = 0U;
  UINT hour = 0U;
  UINT minute = 0U;
  UINT second = 0U;
  CHAR entry_name[CFG_APP_FILEX_ENUM_NAME_LEN];
  uint32_t entry_count = 0U;
  uint32_t candidate_index = 0U;
  uint32_t lower_or_equal_count = 0U;
  uint32_t lower_count = 0U;

  if ((current_snapshot_index == 0U) || (file_name_ptr == NULL) ||
      (file_name_size < CFG_APP_FILEX_SNAPSHOT_NAME_LEN) || (entry_count_ptr == NULL) ||
      (entry_rank_ptr == NULL) || (snapshot_index_ptr == NULL))
  {
    return FX_PTR_ERROR;
  }

  *entry_count_ptr = 0U;
  *entry_rank_ptr = 0U;
  *snapshot_index_ptr = 0U;
  file_name_ptr[0] = '\0';

  status = tx_mutex_get(&g_app_filex_mutex, TX_WAIT_FOREVER);
  if (status != TX_SUCCESS)
  {
    return status;
  }

  status = app_filex_ensure_ready_locked();
  if (status == FX_SUCCESS)
  {
    status = fx_directory_default_set(&sdio_disk, "/" CFG_APP_FILEX_SNAPSHOT_DIR_NAME);
  }
  if (status == FX_SUCCESS)
  {
    status = fx_directory_first_full_entry_find(&sdio_disk,
                                                entry_name,
                                                &attributes,
                                                &size,
                                                &year,
                                                &month,
                                                &day,
                                                &hour,
                                                &minute,
                                                &second);
  }
  while (status == FX_SUCCESS)
  {
    if (((attributes & FX_DIRECTORY) == 0U) && (app_filex_is_snapshot_name(entry_name) != 0U))
    {
      uint32_t snapshot_index = app_filex_parse_snapshot_index(entry_name);
      if (snapshot_index > 0U)
      {
        entry_count++;
        if (snapshot_index <= current_snapshot_index)
        {
          lower_or_equal_count++;
        }
        if (snapshot_index < current_snapshot_index)
        {
          lower_count++;
        }

        if (next != 0U)
        {
          if ((snapshot_index > current_snapshot_index) &&
              ((candidate_index == 0U) || (snapshot_index < candidate_index)))
          {
            candidate_index = snapshot_index;
          }
        }
        else
        {
          if ((snapshot_index < current_snapshot_index) && (snapshot_index > candidate_index))
          {
            candidate_index = snapshot_index;
          }
        }
      }
    }

    status = fx_directory_next_full_entry_find(&sdio_disk,
                                               entry_name,
                                               &attributes,
                                               &size,
                                               &year,
                                               &month,
                                               &day,
                                               &hour,
                                               &minute,
                                               &second);
  }

  (void)fx_directory_default_set(&sdio_disk, "/");
  if (status == FX_NO_MORE_ENTRIES)
  {
    status = FX_SUCCESS;
  }
  if ((status == FX_SUCCESS) && (candidate_index == 0U))
  {
    status = FX_NO_MORE_ENTRIES;
  }
  if (status == FX_SUCCESS)
  {
    status = app_filex_build_snapshot_name(candidate_index, file_name_ptr, file_name_size);
  }
  if (status == FX_SUCCESS)
  {
    *entry_count_ptr = entry_count;
    *snapshot_index_ptr = candidate_index;
    *entry_rank_ptr = (next != 0U) ? lower_or_equal_count : (lower_count - 1U);
  }

  g_app_filex_last_status = status;
  (void)tx_mutex_put(&g_app_filex_mutex);
  return status;
}
UINT app_filex_snapshot_load_rgb565(const CHAR *file_name_ptr,
                                    uint16_t *frame_buffer_ptr,
                                    uint32_t frame_buffer_pixels,
                                    uint16_t *width_ptr,
                                    uint16_t *height_ptr)
{
  UINT status;
  FX_FILE file_handle;
  uint8_t header_buffer[CFG_APP_FILEX_BMP_HEADER_SIZE];
  uint16_t width;
  uint16_t height;
  int32_t signed_height;
  uint32_t image_pixels;
  ULONG actual_size = 0UL;
  uint8_t file_opened = 0U;

  if ((file_name_ptr == NULL) || (frame_buffer_ptr == NULL) || (width_ptr == NULL) || (height_ptr == NULL))
  {
    return FX_PTR_ERROR;
  }

  status = tx_mutex_get(&g_app_filex_mutex, TX_WAIT_FOREVER);
  if (status != TX_SUCCESS)
  {
    return status;
  }

  status = app_filex_ensure_ready_locked();
  if (status == FX_SUCCESS)
  {
    status = fx_directory_default_set(&sdio_disk, "/" CFG_APP_FILEX_SNAPSHOT_DIR_NAME);
  }
  if (status == FX_SUCCESS)
  {
    status = fx_file_open(&sdio_disk, &file_handle, (CHAR *)file_name_ptr, FX_OPEN_FOR_READ);
    if (status == FX_SUCCESS)
    {
      file_opened = 1U;
    }
  }
  if (status == FX_SUCCESS)
  {
    status = fx_file_read(&file_handle, header_buffer, sizeof(header_buffer), &actual_size);
    if ((status == FX_SUCCESS) && (actual_size != sizeof(header_buffer)))
    {
      status = FX_IO_ERROR;
    }
  }
  if (status == FX_SUCCESS)
  {
    if ((header_buffer[0] != 'B') || (header_buffer[1] != 'M'))
    {
      status = FX_INVALID_PATH;
    }
  }
  if (status == FX_SUCCESS)
  {
    width = app_filex_read_le16(&header_buffer[18]);
    signed_height = app_filex_read_le32s(&header_buffer[22]);
    height = (uint16_t)((signed_height < 0) ? (-signed_height) : signed_height);

    if ((app_filex_read_le16(&header_buffer[26]) != CFG_APP_FILEX_BMP_PLANES) ||
        (app_filex_read_le16(&header_buffer[28]) != CFG_APP_FILEX_BMP_BPP_RGB565) ||
        (app_filex_read_le32(&header_buffer[30]) != CFG_APP_FILEX_BMP_COMPRESSION_BITFIELDS) ||
        (app_filex_read_le32(&header_buffer[54]) != 0x0000F800UL) ||
        (app_filex_read_le32(&header_buffer[58]) != 0x000007E0UL) ||
        (app_filex_read_le32(&header_buffer[62]) != 0x0000001FUL) ||
        (signed_height >= 0))
    {
      status = FX_INVALID_PATH;
    }
    else
    {
      image_pixels = (uint32_t)width * (uint32_t)height;
      if ((image_pixels > frame_buffer_pixels) || (width == 0U) || (height == 0U))
      {
        status = FX_BUFFER_ERROR;
      }
    }
  }
  if (status == FX_SUCCESS)
  {
    status = fx_file_seek(&file_handle, CFG_APP_FILEX_BMP_HEADER_SIZE);
  }
  if (status == FX_SUCCESS)
  {
    status = fx_file_read(&file_handle,
                          frame_buffer_ptr,
                          (ULONG)((uint32_t)width * (uint32_t)height * sizeof(uint16_t)),
                          &actual_size);
    if ((status == FX_SUCCESS) &&
        (actual_size != (ULONG)((uint32_t)width * (uint32_t)height * sizeof(uint16_t))))
    {
      status = FX_IO_ERROR;
    }
  }
  if (status == FX_SUCCESS)
  {
    *width_ptr = width;
    *height_ptr = height;
  }

  if (file_opened != 0U)
  {
    (void)fx_file_close(&file_handle);
  }
  (void)fx_directory_default_set(&sdio_disk, "/");
  g_app_filex_last_status = status;
  (void)tx_mutex_put(&g_app_filex_mutex);

  return status;
}

/**
  * @brief  Delete one saved BMP snapshot from FileX storage.
  * @param  file_name_ptr Snapshot file name in THMxxxxx.BMP format.
  * @retval FileX status code.
  */
UINT app_filex_snapshot_delete(const CHAR *file_name_ptr)
{
  UINT status;

  if (file_name_ptr == NULL)
  {
    return FX_PTR_ERROR;
  }

  status = tx_mutex_get(&g_app_filex_mutex, TX_WAIT_FOREVER);
  if (status != TX_SUCCESS)
  {
    return status;
  }

  status = app_filex_ensure_ready_locked();
  if (status == FX_SUCCESS)
  {
    status = fx_directory_default_set(&sdio_disk, "/" CFG_APP_FILEX_SNAPSHOT_DIR_NAME);
  }
  if (status == FX_SUCCESS)
  {
    status = fx_file_delete(&sdio_disk, (CHAR *)file_name_ptr);
  }
  if (status == FX_SUCCESS)
  {
    status = fx_media_flush(&sdio_disk);
  }

  (void)fx_directory_default_set(&sdio_disk, "/");
  g_app_filex_last_status = status;
  (void)tx_mutex_put(&g_app_filex_mutex);

  return status;
}

/**
  * @brief  Convert the last FileX status to a short UI string.
  * @param  status FileX status code.
  * @retval const char* Static status string.
  */
const char *app_filex_status_to_string(UINT status)
{
  return app_filex_status_to_string_internal(status);
}

/**
  * @brief  Mount the SD media and ensure the thermal snapshot directory exists.
  * @retval FileX status code.
  */
static UINT app_filex_mount_media_locked(void)
{
  UINT status;

  status = fx_media_open(&sdio_disk,
                         FX_SD_VOLUME_NAME,
                         fx_stm32_sd_driver,
                         (VOID *)FX_NULL,
                         (VOID *)fx_sd_media_memory,
                         sizeof(fx_sd_media_memory));

  if (status != FX_SUCCESS)
  {
    status = app_filex_format_media_locked();
    if (status != FX_SUCCESS)
    {
      return status;
    }

    status = fx_media_open(&sdio_disk,
                           FX_SD_VOLUME_NAME,
                           fx_stm32_sd_driver,
                           (VOID *)FX_NULL,
                           (VOID *)fx_sd_media_memory,
                           sizeof(fx_sd_media_memory));
    if (status != FX_SUCCESS)
    {
      return status;
    }
  }

  status = fx_directory_default_set(&sdio_disk, "/");
  if (status != FX_SUCCESS)
  {
    return status;
  }

  status = fx_directory_create(&sdio_disk, CFG_APP_FILEX_SNAPSHOT_DIR_NAME);
  if (status == FX_ALREADY_CREATED)
  {
    status = FX_SUCCESS;
  }
  if (status == FX_SUCCESS)
  {
    status = fx_media_flush(&sdio_disk);
  }

  return status;
}

/**
  * @brief  Format the SD media using a compact FAT layout.
  * @retval FileX status code.
  */
static UINT app_filex_format_media_locked(void)
{
  HAL_SD_CardInfoTypeDef card_info;
  UINT status;
  ULONG total_sectors;
  UINT bytes_per_sector;
  UINT sectors_per_cluster;

  if (HAL_SD_GetCardInfo(&hsd2, &card_info) != HAL_OK)
  {
    return FX_IO_ERROR;
  }

  total_sectors = (ULONG)card_info.LogBlockNbr;
  bytes_per_sector = (UINT)card_info.LogBlockSize;

  if ((total_sectors == 0UL) || (bytes_per_sector == 0U))
  {
    return FX_IO_ERROR;
  }

  if (total_sectors < 0x10000UL)
  {
    sectors_per_cluster = 1U;
  }
  else if (total_sectors < 0x20000UL)
  {
    sectors_per_cluster = 2U;
  }
  else if (total_sectors < 0x40000UL)
  {
    sectors_per_cluster = 4U;
  }
  else if (total_sectors < 0x800000UL)
  {
    sectors_per_cluster = 8U;
  }
  else
  {
    sectors_per_cluster = 16U;
  }

  status = fx_media_format(&sdio_disk,
                           fx_stm32_sd_driver,
                           (VOID *)FX_NULL,
                           (UCHAR *)fx_sd_media_memory,
                           sizeof(fx_sd_media_memory),
                           (CHAR *)CFG_APP_FILEX_VOLUME_LABEL,
                           FX_SD_NUMBER_OF_FATS,
                           CFG_APP_FILEX_DEFAULT_DIR_ENTRIES,
                           FX_SD_HIDDEN_SECTORS,
                           total_sectors,
                           bytes_per_sector,
                           sectors_per_cluster,
                           1U,
                           1U);

  return status;
}

/**
  * @brief  Ensure the FileX media is mounted before an operation.
  * @retval FileX status code.
  */
static UINT app_filex_ensure_ready_locked(void)
{
  UINT status = FX_SUCCESS;

  if (g_app_filex_ready == 0U)
  {
    status = app_filex_mount_media_locked();
    g_app_filex_ready = (status == FX_SUCCESS) ? 1U : 0U;
  }

  return status;
}

/**
  * @brief  Collect snapshot file entries from the THERMAL directory.
  * @param  entry_list_ptr Snapshot entry output array.
  * @param  entry_capacity Number of entries available in @p entry_list_ptr.
  * @param  entry_count_ptr Returns the number of collected entries.
  * @param  max_index_ptr Returns the highest snapshot index found.
  * @retval FileX status code.
  */
static UINT app_filex_collect_entries_locked(app_filex_snapshot_entry_t *entry_list_ptr,
                                             uint32_t entry_capacity,
                                             uint32_t *entry_count_ptr,
                                             uint32_t *max_index_ptr)
{
  UINT status;
  UINT attributes = 0U;
  ULONG size = 0UL;
  UINT year = 0U;
  UINT month = 0U;
  UINT day = 0U;
  UINT hour = 0U;
  UINT minute = 0U;
  UINT second = 0U;
  CHAR entry_name[CFG_APP_FILEX_ENUM_NAME_LEN];
  uint32_t entry_count = 0U;
  uint32_t max_index = 0U;

  if ((entry_list_ptr == NULL) || (entry_count_ptr == NULL) || (max_index_ptr == NULL))
  {
    return FX_PTR_ERROR;
  }

  *entry_count_ptr = 0U;
  *max_index_ptr = 0U;

  status = fx_directory_default_set(&sdio_disk, "/" CFG_APP_FILEX_SNAPSHOT_DIR_NAME);
  if (status != FX_SUCCESS)
  {
    return status;
  }

  status = fx_directory_first_full_entry_find(&sdio_disk,
                                              entry_name,
                                              &attributes,
                                              &size,
                                              &year,
                                              &month,
                                              &day,
                                              &hour,
                                              &minute,
                                              &second);
  while (status == FX_SUCCESS)
  {
    uint32_t snapshot_index;

    if (((attributes & FX_DIRECTORY) == 0U) && (app_filex_is_snapshot_name(entry_name) != 0U))
    {
      snapshot_index = app_filex_parse_snapshot_index(entry_name);
      if (snapshot_index > 0U)
      {
        if (snapshot_index > max_index)
        {
          max_index = snapshot_index;
        }

        app_filex_insert_entry_sorted(entry_list_ptr,
                                      &entry_count,
                                      entry_capacity,
                                      snapshot_index,
                                      entry_name);
      }
    }

    status = fx_directory_next_full_entry_find(&sdio_disk,
                                               entry_name,
                                               &attributes,
                                               &size,
                                               &year,
                                               &month,
                                               &day,
                                               &hour,
                                               &minute,
                                               &second);
  }

  (void)fx_directory_default_set(&sdio_disk, "/");

  if (status == FX_NO_MORE_ENTRIES)
  {
    status = FX_SUCCESS;
  }

  *entry_count_ptr = entry_count;
  *max_index_ptr = max_index;
  return status;
}

/**
  * @brief  Insert one snapshot entry into an ascending index list.
  * @param  entry_list_ptr Snapshot entry array.
  * @param  entry_count_ptr Current entry count, updated in place.
  * @param  entry_capacity Array capacity.
  * @param  snapshot_index Parsed numeric snapshot index.
  * @param  file_name_ptr Snapshot file name.
  * @retval None.
  */
static void app_filex_insert_entry_sorted(app_filex_snapshot_entry_t *entry_list_ptr,
                                          uint32_t *entry_count_ptr,
                                          uint32_t entry_capacity,
                                          uint32_t snapshot_index,
                                          const CHAR *file_name_ptr)
{
  uint32_t insert_pos;
  uint32_t shift_index;
  uint32_t entry_count;

  if ((entry_list_ptr == NULL) || (entry_count_ptr == NULL) || (file_name_ptr == NULL) || (entry_capacity == 0U))
  {
    return;
  }

  entry_count = *entry_count_ptr;
  insert_pos = entry_count;

  while ((insert_pos > 0U) && (entry_list_ptr[insert_pos - 1U].snapshot_index > snapshot_index))
  {
    insert_pos--;
  }

  if (entry_count < entry_capacity)
  {
    for (shift_index = entry_count; shift_index > insert_pos; shift_index--)
    {
      entry_list_ptr[shift_index] = entry_list_ptr[shift_index - 1U];
    }
    entry_count++;
  }
  else
  {
    if (insert_pos == 0U)
    {
      return;
    }

    if (insert_pos >= entry_capacity)
    {
      for (shift_index = 0U; shift_index < (entry_capacity - 1U); shift_index++)
      {
        entry_list_ptr[shift_index] = entry_list_ptr[shift_index + 1U];
      }
      insert_pos = entry_capacity - 1U;
    }
    else
    {
      for (shift_index = 0U; shift_index < (insert_pos - 1U); shift_index++)
      {
        entry_list_ptr[shift_index] = entry_list_ptr[shift_index + 1U];
      }
      insert_pos--;
    }
  }

  entry_list_ptr[insert_pos].snapshot_index = snapshot_index;  (void)memset(entry_list_ptr[insert_pos].file_name, 0, sizeof(entry_list_ptr[insert_pos].file_name));
  (void)strncpy((char *)entry_list_ptr[insert_pos].file_name,
                (const char *)file_name_ptr,
                sizeof(entry_list_ptr[insert_pos].file_name) - 1U);

  *entry_count_ptr = entry_count;
}

/**
  * @brief  Parse the numeric index from a snapshot file name.
  * @param  file_name_ptr Snapshot file name in THMxxxxx.BMP format.
  * @retval Parsed index, or 0 when the name is invalid.
  */
static uint32_t app_filex_parse_snapshot_index(const CHAR *file_name_ptr)
{
  uint32_t snapshot_index = 0U;
  uint32_t name_index;

  if (app_filex_is_snapshot_name(file_name_ptr) == 0U)
  {
    return 0U;
  }

  for (name_index = 3U; name_index < 8U; name_index++)
  {
    snapshot_index = (snapshot_index * 10U) + (uint32_t)(file_name_ptr[name_index] - '0');
  }

  return snapshot_index;
}

/**
  * @brief  Check whether a directory entry matches the THMxxxxx.BMP pattern.
  * @param  file_name_ptr Snapshot file name.
  * @retval 1 when the name matches, otherwise 0.
  */
static uint8_t app_filex_is_snapshot_name(const CHAR *file_name_ptr)
{
  uint32_t digit_index;

  if (file_name_ptr == NULL)
  {
    return 0U;
  }

  if ((file_name_ptr[0] != 'T') || (file_name_ptr[1] != 'H') || (file_name_ptr[2] != 'M'))
  {
    return 0U;
  }

  for (digit_index = 3U; digit_index < 8U; digit_index++)
  {
    if ((file_name_ptr[digit_index] < '0') || (file_name_ptr[digit_index] > '9'))
    {
      return 0U;
    }
  }

  if ((file_name_ptr[8] != '.') ||
      (file_name_ptr[9] != 'B') ||
      (file_name_ptr[10] != 'M') ||
      (file_name_ptr[11] != 'P') ||
      (file_name_ptr[12] != '\0'))
  {
    return 0U;
  }

  return 1U;
}

/**
  * @brief  Build one THMxxxxx.BMP file name from a numeric index.
  * @param  snapshot_index Sequential snapshot index.
  * @param  file_name_ptr Output file name buffer.
  * @param  file_name_size Size of @p file_name_ptr in bytes.
  * @retval FileX status code.
  */
static UINT app_filex_build_snapshot_name(uint32_t snapshot_index, CHAR *file_name_ptr, uint32_t file_name_size)
{
  if ((file_name_ptr == NULL) || (file_name_size < CFG_APP_FILEX_SNAPSHOT_NAME_LEN) || (snapshot_index > 99999U))
  {
    return FX_PTR_ERROR;
  }

  (void)snprintf((char *)file_name_ptr, (size_t)file_name_size, "THM%05lu.BMP", (unsigned long)snapshot_index);
  return FX_SUCCESS;
}

/**
  * @brief  Serialize one top-down RGB565 BMP header.
  * @param  header_buffer_ptr Output buffer of CFG_APP_FILEX_BMP_HEADER_SIZE bytes.
  * @param  width Snapshot width in pixels.
  * @param  height Snapshot height in pixels.
  * @retval None.
  */
static void app_filex_build_bmp_header(uint8_t *header_buffer_ptr, uint16_t width, uint16_t height)
{
  uint32_t image_size_bytes;
  uint32_t file_size_bytes;
  int32_t top_down_height;

  if (header_buffer_ptr == NULL)
  {
    return;
  }

  image_size_bytes = (uint32_t)width * (uint32_t)height * sizeof(uint16_t);
  file_size_bytes = CFG_APP_FILEX_BMP_HEADER_SIZE + image_size_bytes;
  top_down_height = -(int32_t)height;

  (void)memset(header_buffer_ptr, 0, CFG_APP_FILEX_BMP_HEADER_SIZE);
  header_buffer_ptr[0] = 'B';
  header_buffer_ptr[1] = 'M';

  app_filex_write_le32(&header_buffer_ptr[2], file_size_bytes);
  app_filex_write_le32(&header_buffer_ptr[10], CFG_APP_FILEX_BMP_HEADER_SIZE);
  app_filex_write_le32(&header_buffer_ptr[14], CFG_APP_FILEX_BMP_INFO_HEADER_SIZE);
  app_filex_write_le32(&header_buffer_ptr[18], width);
  app_filex_write_le32(&header_buffer_ptr[22], (uint32_t)top_down_height);
  app_filex_write_le16(&header_buffer_ptr[26], CFG_APP_FILEX_BMP_PLANES);
  app_filex_write_le16(&header_buffer_ptr[28], CFG_APP_FILEX_BMP_BPP_RGB565);
  app_filex_write_le32(&header_buffer_ptr[30], CFG_APP_FILEX_BMP_COMPRESSION_BITFIELDS);
  app_filex_write_le32(&header_buffer_ptr[34], image_size_bytes);
  app_filex_write_le32(&header_buffer_ptr[38], CFG_APP_FILEX_BMP_PPM);
  app_filex_write_le32(&header_buffer_ptr[42], CFG_APP_FILEX_BMP_PPM);
  app_filex_write_le32(&header_buffer_ptr[54], 0x0000F800UL);
  app_filex_write_le32(&header_buffer_ptr[58], 0x000007E0UL);
  app_filex_write_le32(&header_buffer_ptr[62], 0x0000001FUL);
}

/**
  * @brief  Write one 16-bit little-endian value into a byte buffer.
  * @param  dest_ptr Destination byte pointer.
  * @param  value Value to serialize.
  * @retval None.
  */
static void app_filex_write_le16(uint8_t *dest_ptr, uint16_t value)
{
  if (dest_ptr == NULL)
  {
    return;
  }

  dest_ptr[0] = (uint8_t)(value & 0xFFU);
  dest_ptr[1] = (uint8_t)((value >> 8) & 0xFFU);
}

/**
  * @brief  Write one 32-bit little-endian value into a byte buffer.
  * @param  dest_ptr Destination byte pointer.
  * @param  value Value to serialize.
  * @retval None.
  */
static void app_filex_write_le32(uint8_t *dest_ptr, uint32_t value)
{
  if (dest_ptr == NULL)
  {
    return;
  }

  dest_ptr[0] = (uint8_t)(value & 0xFFU);
  dest_ptr[1] = (uint8_t)((value >> 8) & 0xFFU);
  dest_ptr[2] = (uint8_t)((value >> 16) & 0xFFU);
  dest_ptr[3] = (uint8_t)((value >> 24) & 0xFFU);
}

/**
  * @brief  Read one 16-bit little-endian value from a byte buffer.
  * @param  src_ptr Source byte pointer.
  * @retval Parsed 16-bit value.
  */
static uint16_t app_filex_read_le16(const uint8_t *src_ptr)
{
  if (src_ptr == NULL)
  {
    return 0U;
  }

  return (uint16_t)((uint16_t)src_ptr[0] | ((uint16_t)src_ptr[1] << 8));
}

/**
  * @brief  Read one 32-bit little-endian value from a byte buffer.
  * @param  src_ptr Source byte pointer.
  * @retval Parsed 32-bit value.
  */
static uint32_t app_filex_read_le32(const uint8_t *src_ptr)
{
  if (src_ptr == NULL)
  {
    return 0UL;
  }

  return ((uint32_t)src_ptr[0]) |
         ((uint32_t)src_ptr[1] << 8) |
         ((uint32_t)src_ptr[2] << 16) |
         ((uint32_t)src_ptr[3] << 24);
}

/**
  * @brief  Read one signed 32-bit little-endian value from a byte buffer.
  * @param  src_ptr Source byte pointer.
  * @retval Parsed signed 32-bit value.
  */
static int32_t app_filex_read_le32s(const uint8_t *src_ptr)
{
  return (int32_t)app_filex_read_le32(src_ptr);
}

/**
  * @brief  Convert a FileX status code to a short UI-friendly string.
  * @param  status FileX status code.
  * @retval const char* Static status text.
  */
static const char *app_filex_status_to_string_internal(UINT status)
{
  switch (status)
  {
    case FX_SUCCESS:
      return "OK";
    case FX_NOT_OPEN:
      return "Not Ready";
    case FX_IO_ERROR:
      return "IO Error";
    case FX_MEDIA_INVALID:
      return "Bad Media";
    case FX_NO_MORE_ENTRIES:
      return "Empty";
    case FX_PTR_ERROR:
      return "Bad Ptr";
    case FX_BUFFER_ERROR:
      return "Buffer";
    default:
      return "Storage Err";
  }
}
/* USER CODE END 1 */
