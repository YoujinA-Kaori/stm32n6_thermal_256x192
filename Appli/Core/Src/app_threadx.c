/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_threadx.c
  * @author  MCD Application Team
  * @brief   ThreadX applicative file
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
#include "app_threadx.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "main.h"
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "gui_guider.h"
#include "events_init.h"
#include "custom.h"
#include "app_filex.h"
#include "i2c.h"
#include "libirtemp.h"
#include "thermal_ai_runtime.h"
#include "thermal_project_config.h"
#include "BQ27441/bq27441g1a.h"
#include "IMX219/Inc/app_camera_imx219.h"
#include "IMX219/Inc/app_camera_isp.h"
#include "Tiny1C/tiny1c_thermal_app.h"
#include "Tiny1C/tiny1c_vdcmd_app.h"
#include "usart.h"
#include "RGBLCD/rgblcd.h"
#include "dcmipp.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef struct __attribute__((packed))
{
  uint16_t sync_word;
  uint16_t packet_type;
  uint32_t frame_counter;
  uint16_t frame_width;
  uint16_t frame_height;
  uint16_t payload_bytes;
  uint16_t center_temp14;
  uint16_t min_temp14;
  uint16_t max_temp14;
} app_threadx_uart_stream_header_t;

typedef struct
{
  const uint16_t *frame;
  uint32_t counter;
  uint32_t timestamp_ms;
} app_imx219_frame_snapshot_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define CFG_THERMAL_THREAD_STACK_SIZE         4096U
#define CFG_THERMAL_THREAD_PRIORITY           13U
#define CFG_GUI_THREAD_STACK_SIZE             8192U
#define CFG_GUI_THREAD_PRIORITY               10U
#define CFG_UART_STREAM_THREAD_STACK_SIZE     4096U
#define CFG_UART_STREAM_THREAD_PRIORITY       15U
#define CFG_UART_STREAM_SOURCE_WIDTH          CFG_THERMAL_SENSOR_WIDTH
#define CFG_UART_STREAM_SOURCE_HEIGHT         CFG_THERMAL_SENSOR_HEIGHT
#define CFG_UART_STREAM_DOWNSAMPLE_STEP_X     CFG_THERMAL_UART_DOWNSAMPLE_STEP_X
#define CFG_UART_STREAM_DOWNSAMPLE_STEP_Y     CFG_THERMAL_UART_DOWNSAMPLE_STEP_Y
#define CFG_UART_STREAM_FRAME_WIDTH           CFG_THERMAL_UART_FRAME_WIDTH
#define CFG_UART_STREAM_FRAME_HEIGHT          CFG_THERMAL_UART_FRAME_HEIGHT
#define CFG_UART_STREAM_PAYLOAD_BYTES         (CFG_UART_STREAM_FRAME_WIDTH * CFG_UART_STREAM_FRAME_HEIGHT * sizeof(uint16_t))
#define CFG_UART_STREAM_SYNC_WORD             0x55AAU
#define CFG_UART_STREAM_PACKET_TYPE_TEMP14    0x0001U
#define CFG_UART_STREAM_PERIOD_MS             200U
#define CFG_UART_STREAM_PERIOD_TICKS          (((CFG_UART_STREAM_PERIOD_MS * TX_TIMER_TICKS_PER_SECOND) + 999U) / 1000U)
#define CFG_THERMAL_AI_MIN_INTERVAL_MS        125U

/* Active thermal detector: calibrated 256x192 temp14 hotspot boxes. */
#define CFG_THERMAL_AI_ENABLE                 1U
#define CFG_UART_COMMAND_THREAD_STACK_SIZE    4096U
#define CFG_UART_COMMAND_THREAD_PRIORITY      16U
#define CFG_UART_FILE_WEB_TIMEOUT_MS          4000U
#define CFG_UART_COMMAND_LINE_MAX             96U
#define CFG_UART_COMMAND_RX_RING_SIZE          128U
#define CFG_UART_COMMAND_RX_RING_MASK          (CFG_UART_COMMAND_RX_RING_SIZE - 1U)
#define CFG_UART_COMMAND_TX_LINE_MAX          768U
#define CFG_UART_FILE_BASE64_CHUNK_BYTES      384U
#define CFG_UART_FILE_BASE64_CHUNK_TEXT       (((CFG_UART_FILE_BASE64_CHUNK_BYTES + 2U) / 3U) * 4U)
#define CFG_UART_FILE_MAX_LISTED_SNAPSHOTS    32U
#define CFG_UART_FILE_NAME_STRIDE             CFG_APP_FILEX_SNAPSHOT_NAME_LEN
#define CFG_UART_FILE_MAX_WIDTH               640U
#define CFG_UART_FILE_MAX_HEIGHT              480U
#define CFG_UART_FILE_MAX_PIXELS              (CFG_UART_FILE_MAX_WIDTH * CFG_UART_FILE_MAX_HEIGHT)
#define CFG_UART_FILE_WEB_TIMEOUT_TICKS       (((CFG_UART_FILE_WEB_TIMEOUT_MS * TX_TIMER_TICKS_PER_SECOND) + 999U) / 1000U)
#define CFG_LIBIRTEMP_TEST_EMISSIVITY         0.95F
#define CFG_LIBIRTEMP_TEST_TAU_Q14            16384U
#define CFG_LIBIRTEMP_TEST_AMBIENT_TEMP_C     25.0F
#define CFG_GUI_PREVIEW_WIDTH                 432U
#define CFG_GUI_PREVIEW_HEIGHT                324U
#define CFG_GUI_FULLSCREEN_WIDTH              640U
#define CFG_GUI_FULLSCREEN_HEIGHT             480U
#define CFG_CAMERA_CANVAS_WIDTH               512U
#define CFG_CAMERA_CANVAS_HEIGHT              384U
#define CFG_CAMERA_ALIGNMENT_SCALE_MIN        500U
#define CFG_CAMERA_ALIGNMENT_SCALE_MAX        2000U
#define CFG_CAMERA_ALIGNMENT_OFFSET_MIN_X     (-256)
#define CFG_CAMERA_ALIGNMENT_OFFSET_MAX_X     256
#define CFG_CAMERA_ALIGNMENT_OFFSET_MIN_Y     (-192)
#define CFG_CAMERA_ALIGNMENT_OFFSET_MAX_Y     192
#define CFG_CAMERA_ALIGNMENT_DEFAULT_SCALE    1000U
#define CFG_CAMERA_ALIGNMENT_DEFAULT_ALPHA    128U
#define CFG_CAMERA_ALIGNMENT_ROTATION_MIN      (-10)
#define CFG_CAMERA_ALIGNMENT_ROTATION_MAX      10
#define CFG_CAMERA_SYNC_MAX_SKEW_MS            50U
#define CFG_GUI_OVERLAY_UPDATE_PERIOD_MS      125U
#define CFG_EXTREMA_UPDATE_THREAD_STACK_SIZE  2048U
#define CFG_EXTREMA_UPDATE_THREAD_PRIORITY    18U
#define CFG_EXTREMA_UPDATE_PERIOD_MS          125U
#define CFG_EXTREMA_UPDATE_PERIOD_TICKS       (((CFG_EXTREMA_UPDATE_PERIOD_MS * TX_TIMER_TICKS_PER_SECOND) + 999U) / 1000U)
#define CFG_BATTERY_THREAD_STACK_SIZE         2048U
#define CFG_BATTERY_THREAD_PRIORITY           19U
#define CFG_BATTERY_POLL_PERIOD_MS            1000U
#define CFG_BATTERY_POLL_PERIOD_TICKS         (((CFG_BATTERY_POLL_PERIOD_MS * TX_TIMER_TICKS_PER_SECOND) + 999U) / 1000U)
#define CFG_BATTERY_I2C_TIMEOUT_MS            100U
#define CFG_IMX219_THREAD_STACK_SIZE           3072U
#define CFG_IMX219_THREAD_PRIORITY             20U
#define CFG_IMX219_STARTUP_DELAY_MS            2000U
#define CFG_IMX219_RETRY_DELAY_MS              2000U
#define CFG_IMX219_CONTROL_PERIOD_MS           1000U
#define CFG_IMX219_NO_FRAME_TIMEOUT_MS         1500U
#define CFG_IMX219_FRAME_WIDTH                 640U
#define CFG_IMX219_FRAME_HEIGHT                480U
#define CFG_IMX219_FRAME_BYTES                 (CFG_IMX219_FRAME_WIDTH * CFG_IMX219_FRAME_HEIGHT * sizeof(uint16_t))
#define CFG_THERMAL_AI_INPUT_WIDTH            CFG_THERMAL_SENSOR_WIDTH
#define CFG_THERMAL_AI_INPUT_HEIGHT           CFG_THERMAL_SENSOR_HEIGHT
#define CFG_THERMAL_AI_INPUT_PIXELS           (CFG_THERMAL_AI_INPUT_WIDTH * CFG_THERMAL_AI_INPUT_HEIGHT)
#define CFG_THERMAL_AI_HISTOGRAM_BINS         16384U
#define CFG_THERMAL_AI_BACKGROUND_PERCENT_NUM 50U
#define CFG_THERMAL_AI_BACKGROUND_PERCENT_DEN 100U
#define CFG_THERMAL_AI_VALID_TEMP14_MIN       3730U
#define CFG_THERMAL_AI_ABNORMAL_MIN_AREA_PX   1080.0f
#define CFG_THERMAL_AI_ABNORMAL_MAX_AREA_PX   6400.0f
#define CFG_THERMAL_AI_HOTSPOT_FALLBACK_DELTA_TEMP14 120U
#define CFG_THERMAL_AI_TOPRIGHT_HOTSPOT_DELTA_TEMP14 160U
#define CFG_THERMAL_AI_HOTSPOT_FALLBACK_MIN_PIXELS   100U
#define CFG_THERMAL_AI_HOTSPOT_FALLBACK_BOX_MIN_PX   38U
#define CFG_THERMAL_AI_HOTSPOT_FALLBACK_BOX_MAX_PX   80U
#define CFG_THERMAL_AI_HOTSPOT_FALLBACK_CONFIDENCE_MIN_PERMILLE 550U
#define CFG_THERMAL_AI_HOTSPOT_FALLBACK_CONFIDENCE_MAX_PERMILLE 920U
#define CFG_THERMAL_AI_PREVIEW_CONFIRM_VALID_MAX_TEMP14 16383U
#define CFG_THERMAL_AI_TOPRIGHT_BIAS_COMP_ENABLE     1U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
static TX_THREAD g_thermal_thread;
static TX_THREAD g_gui_thread;
static TX_THREAD g_uart_stream_thread;
static TX_THREAD g_uart_command_thread;
static TX_THREAD g_extrema_update_thread;
static TX_THREAD g_battery_thread;
static TX_THREAD g_imx219_thread;
static TX_MUTEX g_thermal_ai_mutex;
static ULONG g_thermal_thread_stack[CFG_THERMAL_THREAD_STACK_SIZE / sizeof(ULONG)];
static ULONG g_gui_thread_stack[CFG_GUI_THREAD_STACK_SIZE / sizeof(ULONG)];
static ULONG g_uart_stream_thread_stack[CFG_UART_STREAM_THREAD_STACK_SIZE / sizeof(ULONG)];
static ULONG g_uart_command_thread_stack[CFG_UART_COMMAND_THREAD_STACK_SIZE / sizeof(ULONG)];
static ULONG g_extrema_update_thread_stack[CFG_EXTREMA_UPDATE_THREAD_STACK_SIZE / sizeof(ULONG)];
static ULONG g_battery_thread_stack[CFG_BATTERY_THREAD_STACK_SIZE / sizeof(ULONG)];
static ULONG g_imx219_thread_stack[CFG_IMX219_THREAD_STACK_SIZE / sizeof(ULONG)];
static uint16_t g_imx219_rgb565_frame_a[CFG_IMX219_FRAME_WIDTH * CFG_IMX219_FRAME_HEIGHT]
  __attribute__((section(".EXTRAM"), aligned(32)));
static uint16_t g_imx219_rgb565_frame_b[CFG_IMX219_FRAME_WIDTH * CFG_IMX219_FRAME_HEIGHT]
  __attribute__((section(".EXTRAM"), aligned(32)));
volatile int32_t g_imx219_runtime_status = APP_CAMERA_IMX219_ERROR_I2C;
volatile uint16_t g_imx219_runtime_chip_id = 0U;
volatile uint32_t g_imx219_runtime_frame_counter = 0U;
volatile uint32_t g_imx219_last_frame_timestamp_ms = 0U;
volatile uint32_t g_imx219_recovery_count = 0U;
volatile uint32_t g_imx219_error_count = 0U;
volatile uint32_t g_imx219_last_error_detail = 0U;
/* SWD-visible camera diagnostics. They never participate in control flow. */
volatile uint32_t g_imx219_no_frame_count = 0U;
volatile uint32_t g_imx219_dcmipp_error_count = 0U;
volatile uint32_t g_imx219_csi_line_error_count = 0U;
volatile uint32_t g_imx219_last_dcmipp_error = 0U;
volatile uint32_t g_imx219_last_csi_error_lane = 0U;
volatile uint32_t g_imx219_last_csi_error_code = 0U;
volatile uint32_t g_camera_sync_wait_count = 0U;
volatile uint32_t g_camera_sync_last_skew_ms = 0U;
volatile uint32_t g_camera_sync_max_skew_ms = 0U;
volatile uint32_t g_camera_render_drop_count = 0U;
volatile uint32_t g_camera_render_last_ms = 0U;
volatile uint32_t g_camera_render_max_ms = 0U;
static volatile uintptr_t g_imx219_completed_frame_address = 0U;
static volatile uint8_t g_imx219_recovery_requested = 0U;
static volatile int32_t g_imx219_recovery_reason = APP_CAMERA_IMX219_ERROR_DCMIPP;
static volatile app_camera_view_mode_t g_camera_view_mode = APP_CAMERA_VIEW_THERMAL;
static volatile app_camera_alignment_t g_camera_alignment =
{
  .offset_x = 0,
  .offset_y = 0,
  .scale_permille = CFG_CAMERA_ALIGNMENT_DEFAULT_SCALE,
  .rotation_degrees = 0,
  .transform_flags = 0U,
  .visible_alpha = CFG_CAMERA_ALIGNMENT_DEFAULT_ALPHA,
};
static volatile uint32_t g_camera_render_generation = 1U;
static volatile float g_libirtemp_probe_sink_c = 0.0f;
static volatile uint8_t g_uart_stream_tx_busy = 0U;
static volatile uint32_t g_uart_file_hold_mask = 0U;
static volatile ULONG g_uart_web_file_hold_expire_tick = 0U;
static volatile uint8_t g_tiny1c_app_started = 0U;
static volatile uint8_t g_extrema_cache_valid = 0U;
static volatile uint16_t g_extrema_cache_min_temp14 = 0U;
static volatile uint16_t g_extrema_cache_max_temp14 = 0U;
static volatile uint16_t g_extrema_cache_min_temp_x = 0U;
static volatile uint16_t g_extrema_cache_min_temp_y = 0U;
static volatile uint16_t g_extrema_cache_max_temp_x = 0U;
static volatile uint16_t g_extrema_cache_max_temp_y = 0U;
static volatile uint8_t g_battery_cache_valid = 0U;
static volatile uint8_t g_battery_cache_percent = 0U;
static volatile uint8_t g_battery_cache_charge_state = (uint8_t)BQ27441_CHARGE_STATUS_SLEEP;
static BQ27441_Device_t g_battery_device;
static uint8_t g_uart_stream_tx_buffer[sizeof(app_threadx_uart_stream_header_t) + CFG_UART_STREAM_PAYLOAD_BYTES]
  __attribute__((aligned(32)));
static uint8_t g_uart_command_rx_line[CFG_UART_COMMAND_LINE_MAX];
static uint8_t g_uart_command_rx_ring[CFG_UART_COMMAND_RX_RING_SIZE];
static uint8_t g_uart_command_rx_irq_byte;
static volatile uint16_t g_uart_command_rx_head = 0U;
static volatile uint16_t g_uart_command_rx_tail = 0U;
static uint8_t g_uart_command_tx_line[CFG_UART_COMMAND_TX_LINE_MAX];
static uint8_t g_uart_file_chunk_base64[CFG_UART_FILE_BASE64_CHUNK_TEXT + 4U];
static CHAR g_uart_file_snapshot_names[CFG_UART_FILE_MAX_LISTED_SNAPSHOTS][CFG_UART_FILE_NAME_STRIDE];
static lv_img_dsc_t g_gui_preview_img_dsc;
static lv_img_dsc_t g_gui_fullscreen_img_dsc;
static uint16_t g_gui_preview_rgb565_frame[CFG_GUI_PREVIEW_WIDTH * CFG_GUI_PREVIEW_HEIGHT]
  __attribute__((section(".EXTRAM"), aligned(32)));
static uint16_t g_gui_fullscreen_rgb565_frame[CFG_GUI_FULLSCREEN_WIDTH * CFG_GUI_FULLSCREEN_HEIGHT]
  __attribute__((section(".EXTRAM"), aligned(32)));
static uint16_t g_gui_camera_canvas_rgb565[CFG_CAMERA_CANVAS_WIDTH * CFG_CAMERA_CANVAS_HEIGHT]
  __attribute__((section(".EXTRAM"), aligned(32)));
static uint16_t g_uart_file_rgb565_frame[CFG_UART_FILE_MAX_PIXELS]
  __attribute__((section(".EXTRAM"), aligned(32)));
static uint16_t g_thermal_ai_oriented_temp14[CFG_THERMAL_AI_INPUT_PIXELS];
static uint16_t g_thermal_ai_histogram[CFG_THERMAL_AI_HISTOGRAM_BINS];
static thermal_ai_runtime_t g_thermal_ai_runtime;
static volatile uint8_t g_thermal_ai_runtime_enable = 0U;
static uint8_t g_thermal_ai_model_ready = 0U;
static uint32_t g_thermal_ai_last_inference_ms = 0U;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static VOID app_threadx_thermal_thread_entry(ULONG thread_input);
static VOID app_threadx_gui_thread_entry(ULONG thread_input);
static VOID app_threadx_uart_stream_thread_entry(ULONG thread_input);
static VOID app_threadx_uart_command_thread_entry(ULONG thread_input);
static VOID app_threadx_extrema_update_thread_entry(ULONG thread_input);
static VOID app_threadx_battery_thread_entry(ULONG thread_input);
static VOID app_threadx_imx219_thread_entry(ULONG thread_input);
static void app_threadx_uart_process_command_line(const uint8_t *command_line_ptr);
static uint8_t app_threadx_uart_command_rx_pop(uint8_t *rx_byte_ptr);
static UINT app_threadx_uart_send_text(const char *text_ptr);
static UINT app_threadx_uart_send_text_fmt(const char *format_ptr, ...);
static void app_threadx_uart_wait_for_tx_idle(void);
static uint8_t app_threadx_uart_file_mode_active_internal(void);
static void app_threadx_uart_file_hold_set(uint32_t hold_mask);
static void app_threadx_uart_file_hold_clear(uint32_t hold_mask);
static UINT app_threadx_uart_send_snapshot_list(void);
static UINT app_threadx_uart_send_snapshot_latest(void);
static UINT app_threadx_uart_send_snapshot_named(const CHAR *file_name_ptr);
static uint32_t app_threadx_base64_encode(const uint8_t *input_ptr,
                                          uint32_t input_size,
                                          char *output_ptr,
                                          uint32_t output_capacity);
static uint32_t app_threadx_uart_stream_build_packet(uint8_t *tx_buffer, const uint16_t *temp14_frame, uint32_t frame_counter);
static void app_threadx_uart_stream_clean_dcache(void *buffer_addr, uint32_t buffer_size);
static int32_t app_threadx_temp14_to_centi_celsius(uint16_t temp14_value);
static int32_t app_threadx_temp14_to_compensated_centi_celsius(uint16_t temp14_value);
static int32_t app_threadx_centi_celsius_to_unit_centi_celsius(int32_t temp_centi_c, uint8_t unit_celsius);
static int32_t app_threadx_float_to_centi_celsius(float temp_c);
static uint16_t app_threadx_temp14_get_preview_oriented_sample(const uint16_t *temp14_frame,
                                                               uint16_t frame_width,
                                                               uint16_t frame_height,
                                                               uint16_t preview_x,
                                                               uint16_t preview_y);
static uint8_t app_threadx_thermal_ai_is_topright_bias_region(uint16_t x,
                                                              uint16_t y);
static void app_threadx_gui_set_badge_text(lv_obj_t *badge, const char *text);
static uint8_t app_threadx_gui_update_preview(lv_ui *ui);
static void app_threadx_gui_format_battery_text(char *buffer, uint32_t buffer_size);
static void app_threadx_gui_format_ai_status_text(char *buffer, uint32_t buffer_size);
static void app_threadx_gui_update_preview_layer(lv_obj_t *preview_img,
                                                 lv_obj_t *preview_center_temp,
                                                 lv_obj_t *preview_max_temp,
                                                 lv_obj_t *preview_min_temp,
                                                 lv_obj_t *preview_max_marker,
                                                 lv_obj_t *preview_min_marker,
                                                 uint16_t preview_width,
                                                 uint16_t preview_height,
                                                 uint16_t frame_width,
                                                 uint16_t frame_height,
                                                 uint8_t mirror_enable,
                                                 uint8_t flip_enable,
                                                 uint8_t unit_celsius,
                                                 uint16_t min_temp14,
                                                 uint16_t max_temp14,
                                                 int32_t center_temp_centi_c,
                                                 uint16_t min_temp_x,
                                                 uint16_t min_temp_y,
                                                 uint16_t max_temp_x,
                                                 uint16_t max_temp_y);
static void app_threadx_thermal_ai_run_inference(const uint16_t *temp14_frame, uint32_t frame_counter);
static void app_threadx_thermal_ai_run_temp14_hotspot(const uint16_t *temp14_frame, uint32_t frame_counter);
static uint16_t app_threadx_thermal_ai_prepare_preview_temp14(const uint16_t *temp14_frame);
static uint16_t app_threadx_thermal_ai_percentile_from_histogram(uint32_t pixel_count);
static void app_threadx_thermal_ai_detect_temp14_hotspot(thermal_ai_result_t *result_ptr,
                                                         uint16_t background_temp14);
static uint16_t app_threadx_thermal_ai_calc_hotspot_confidence(uint32_t hot_pixel_count,
                                                               uint32_t box_width,
                                                               uint32_t box_height,
                                                               uint8_t peak_score);
static void app_threadx_dcache_clean_by_addr(void *buffer_addr, uint32_t buffer_size);
static void app_threadx_dcache_invalidate_by_addr(void *buffer_addr, uint32_t buffer_size);
static uint8_t app_threadx_thermal_ai_get_result_snapshot(thermal_ai_result_t *result_ptr);
static void app_threadx_gui_draw_ai_boxes_rgb565(uint16_t *frame,
                                                  uint16_t frame_width,
                                                  uint16_t frame_height,
                                                  const thermal_ai_result_t *result_ptr);
static void app_threadx_gui_build_scaled_frame(const uint16_t *source_frame,
                                               uint16_t *dest_frame,
                                               uint16_t source_width,
                                               uint16_t source_height,
                                               uint16_t dest_width,
                                               uint16_t dest_height);
static uint8_t app_threadx_imx219_get_frame_snapshot(app_imx219_frame_snapshot_t *snapshot);
static uint32_t app_threadx_tick_abs_delta(uint32_t tick_a, uint32_t tick_b);
static int32_t app_threadx_imx219_reconfigure_dcmipp(void);
static void app_threadx_imx219_stop_capture(uint8_t sensor_initialized);
static uint8_t app_threadx_gui_build_camera_canvas(void);
static uint8_t app_threadx_gui_render_camera_frame(uint16_t *dest_frame,
                                                   uint16_t dest_width,
                                                   uint16_t dest_height);
static void app_threadx_gui_format_temp_text(char *buffer, uint32_t buffer_size, const char *prefix, int32_t temp_centi_c, uint8_t unit_celsius);

/* USER CODE END PFP */

/**
  * @brief  Application ThreadX Initialization.
  * @param memory_ptr: memory pointer
  * @retval int
  */
UINT App_ThreadX_Init(VOID *memory_ptr)
{
  UINT ret = TX_SUCCESS;
  /* USER CODE BEGIN App_ThreadX_MEM_POOL */
  TX_PARAMETER_NOT_USED(memory_ptr);

  /* USER CODE END App_ThreadX_MEM_POOL */
  /* USER CODE BEGIN App_ThreadX_Init */
  ret = app_i2c4_bus_mutex_init();
  if (ret != TX_SUCCESS)
  {
    return ret;
  }

  ret = tx_mutex_create(&g_thermal_ai_mutex, "thermal_ai_mutex", TX_NO_INHERIT);
  if (ret != TX_SUCCESS)
  {
    return ret;
  }

  thermal_ai_runtime_init(&g_thermal_ai_runtime, 2U, 3000);

  ret = tx_thread_create(&g_thermal_thread,
                         "thermal_thread",
                         app_threadx_thermal_thread_entry,
                         0U,
                         g_thermal_thread_stack,
                         sizeof(g_thermal_thread_stack),
                         CFG_THERMAL_THREAD_PRIORITY,
                         CFG_THERMAL_THREAD_PRIORITY,
                         TX_NO_TIME_SLICE,
                         TX_AUTO_START);
  if (ret != TX_SUCCESS)
  {
    return ret;
  }

  ret = tx_thread_create(&g_gui_thread,
                         "gui_thread",
                         app_threadx_gui_thread_entry,
                         0U,
                         g_gui_thread_stack,
                         sizeof(g_gui_thread_stack),
                         CFG_GUI_THREAD_PRIORITY,
                         CFG_GUI_THREAD_PRIORITY,
                         TX_NO_TIME_SLICE,
                         TX_AUTO_START);
  if (ret != TX_SUCCESS)
  {
    return ret;
  }

  ret = tx_thread_create(&g_uart_stream_thread,
                         "uart_stream_thread",
                         app_threadx_uart_stream_thread_entry,
                         0U,
                         g_uart_stream_thread_stack,
                         sizeof(g_uart_stream_thread_stack),
                         CFG_UART_STREAM_THREAD_PRIORITY,
                         CFG_UART_STREAM_THREAD_PRIORITY,
                         TX_NO_TIME_SLICE,
                         TX_AUTO_START);
  if (ret != TX_SUCCESS)
  {
    return ret;
  }

  ret = tx_thread_create(&g_uart_command_thread,
                         "uart_command_thread",
                         app_threadx_uart_command_thread_entry,
                         0U,
                         g_uart_command_thread_stack,
                         sizeof(g_uart_command_thread_stack),
                         CFG_UART_COMMAND_THREAD_PRIORITY,
                         CFG_UART_COMMAND_THREAD_PRIORITY,
                         TX_NO_TIME_SLICE,
                         TX_AUTO_START);
  if (ret != TX_SUCCESS)
  {
    return ret;
  }

  ret = tx_thread_create(&g_extrema_update_thread,
                         "extrema_update_thread",
                         app_threadx_extrema_update_thread_entry,
                         0U,
                         g_extrema_update_thread_stack,
                         sizeof(g_extrema_update_thread_stack),
                         CFG_EXTREMA_UPDATE_THREAD_PRIORITY,
                         CFG_EXTREMA_UPDATE_THREAD_PRIORITY,
                         TX_NO_TIME_SLICE,
                         TX_AUTO_START);
  if (ret != TX_SUCCESS)
  {
    return ret;
  }

  ret = tx_thread_create(&g_battery_thread,
                         "battery_thread",
                         app_threadx_battery_thread_entry,
                         0U,
                         g_battery_thread_stack,
                         sizeof(g_battery_thread_stack),
                         CFG_BATTERY_THREAD_PRIORITY,
                         CFG_BATTERY_THREAD_PRIORITY,
                         TX_NO_TIME_SLICE,
                         TX_AUTO_START);
  if (ret != TX_SUCCESS)
  {
    return ret;
  }

  ret = tx_thread_create(&g_imx219_thread,
                         "imx219_thread",
                         app_threadx_imx219_thread_entry,
                         0U,
                         g_imx219_thread_stack,
                         sizeof(g_imx219_thread_stack),
                         CFG_IMX219_THREAD_PRIORITY,
                         CFG_IMX219_THREAD_PRIORITY,
                         TX_NO_TIME_SLICE,
                         TX_AUTO_START);
  /* USER CODE END App_ThreadX_Init */

  return ret;
}

  /**
  * @brief  Function that implements the kernel's initialization.
  * @param  None
  * @retval None
  */
void MX_ThreadX_Init(void)
{
  /* USER CODE BEGIN Before_Kernel_Start */

  /* USER CODE END Before_Kernel_Start */

  tx_kernel_enter();

  /* USER CODE BEGIN Kernel_Start_Error */

  /* USER CODE END Kernel_Start_Error */
}

/* USER CODE BEGIN 1 */
/**
  * @brief  Request UART file mode for one owner.
  * @param  hold_mask Bit mask describing the requester.
  * @retval None
  */
void app_uart_request_file_mode(uint32_t hold_mask)
{
  if (hold_mask != 0U)
  {
    g_uart_file_hold_mask |= hold_mask;
    if ((hold_mask & APP_UART_FILE_HOLD_WEB) != 0U)
    {
      g_uart_web_file_hold_expire_tick = tx_time_get() + ((CFG_UART_FILE_WEB_TIMEOUT_TICKS > 0U) ? CFG_UART_FILE_WEB_TIMEOUT_TICKS : 1U);
    }
  }
}

/**
  * @brief  Release UART file mode for one owner.
  * @param  hold_mask Bit mask describing the requester.
  * @retval None
  */
void app_uart_release_file_mode(uint32_t hold_mask)
{
  if (hold_mask != 0U)
  {
    g_uart_file_hold_mask &= ~hold_mask;
    if ((hold_mask & APP_UART_FILE_HOLD_WEB) != 0U)
    {
      g_uart_web_file_hold_expire_tick = 0U;
    }
  }
}

/**
  * @brief  Enable or disable runtime temp14 hotspot detection.
  * @param  enable Non-zero to detect and draw boxes; zero to hide boxes.
  * @retval None
  */
void app_thermal_ai_set_enabled(uint8_t enable)
{
  uint8_t next_enable = (enable != 0U) ? 1U : 0U;

  if (tx_mutex_get(&g_thermal_ai_mutex, TX_WAIT_FOREVER) == TX_SUCCESS)
  {
    g_thermal_ai_runtime_enable = next_enable;
    if (next_enable == 0U)
    {
      thermal_ai_runtime_reset(&g_thermal_ai_runtime);
      g_thermal_ai_model_ready = 0U;
    }
    tx_mutex_put(&g_thermal_ai_mutex);
  }
}

/**
  * @brief  Report whether runtime thermal AI detection is enabled.
  * @retval uint8_t 1 when enabled, otherwise 0.
  */
uint8_t app_thermal_ai_is_enabled(void)
{
  return g_thermal_ai_runtime_enable;
}


/**
  * @brief  Apply libirtemp environment compensation to a temperature sample.
  * @param  emissivity Emissivity in range [0, 1].
  * @param  tau_q14 Atmospheric transmittance in Q14 format.
  * @param  ambient_temp_c Ambient temperature in degrees Celsius.
  * @param  source_temp_c Source temperature in degrees Celsius.
  * @param  corrected_temp_c Output compensated temperature pointer.
  * @retval 0 on success, -1 on error.
  */
static int32_t app_threadx_libirtemp_temp_correct(float emissivity,
                                                  uint16_t tau_q14,
                                                  float ambient_temp_c,
                                                  float source_temp_c,
                                                  float *corrected_temp_c)
{
  irtemp_error_t ret;

  if (corrected_temp_c == NULL)
  {
    return -1;
  }

  ret = temp_correct(emissivity, tau_q14, ambient_temp_c, source_temp_c, corrected_temp_c);
  if (ret != IRTEMP_SUCCESS)
  {
    return -1;
  }

  return 0;
}

/**
  * @brief  Convert one temp14 value to centi-degrees Celsius.
  * @param  temp14_value Raw temperature value in 1/16 K units.
  * @retval Temperature in 0.01 degrees Celsius.
  */
static int32_t app_threadx_temp14_to_centi_celsius(uint16_t temp14_value)
{
  int32_t scaled_temp;

  scaled_temp = ((int32_t)temp14_value * 100 + 8) / 16;
  scaled_temp -= 27315;

  return scaled_temp;
}

/**
  * @brief  Convert one temp14 value to compensated centi-degrees Celsius.
  * @param  temp14_value Raw temperature value in 1/16 K units.
  * @retval Compensated temperature in 0.01 degrees Celsius.
  */
static int32_t app_threadx_temp14_to_compensated_centi_celsius(uint16_t temp14_value)
{
  int32_t raw_temp_centi_c;
  float corrected_temp_c;

  raw_temp_centi_c = app_threadx_temp14_to_centi_celsius(temp14_value);
  corrected_temp_c = (float)raw_temp_centi_c / 100.0f;

  if (app_threadx_libirtemp_temp_correct(CFG_LIBIRTEMP_TEST_EMISSIVITY,
                                         CFG_LIBIRTEMP_TEST_TAU_Q14,
                                         CFG_LIBIRTEMP_TEST_AMBIENT_TEMP_C,
                                         corrected_temp_c,
                                         &corrected_temp_c) == 0)
  {
    return app_threadx_float_to_centi_celsius(corrected_temp_c);
  }

  return raw_temp_centi_c;
}

/**
  * @brief  Convert centi-degrees Celsius into the selected unit.
  * @param  temp_centi_c Temperature in 0.01 degrees Celsius.
  * @param  unit_celsius Non-zero for Celsius, zero for Fahrenheit.
  * @retval Temperature in the selected unit, still scaled by 100.
  */
static int32_t app_threadx_centi_celsius_to_unit_centi_celsius(int32_t temp_centi_c, uint8_t unit_celsius)
{
  if (unit_celsius != 0U)
  {
    return temp_centi_c;
  }

  return ((temp_centi_c * 9) / 5) + 3200;
}

/**
  * @brief  Convert a float temperature to centi-degrees Celsius.
  * @param  temp_c Temperature in degrees Celsius.
  * @retval Temperature in 0.01 degrees Celsius.
  */
static int32_t app_threadx_float_to_centi_celsius(float temp_c)
{
  float scaled_temp;

  scaled_temp = temp_c * 100.0f;
  if (scaled_temp >= 0.0f)
  {
    scaled_temp += 0.5f;
  }
  else
  {
    scaled_temp -= 0.5f;
  }

  return (int32_t)scaled_temp;
}

/**
  * @brief  Read one temp14 sample in the current preview/UART orientation.
  * @param  temp14_frame Pointer to the raw temp14 frame.
  * @param  frame_width Raw frame width.
  * @param  frame_height Raw frame height.
  * @param  preview_x X coordinate in preview-oriented space.
  * @param  preview_y Y coordinate in preview-oriented space.
  * @retval uint16_t Preview-oriented temp14 sample.
  */
static uint16_t app_threadx_temp14_get_preview_oriented_sample(const uint16_t *temp14_frame,
                                                               uint16_t frame_width,
                                                               uint16_t frame_height,
                                                               uint16_t preview_x,
                                                               uint16_t preview_y)
{
  uint16_t source_x = preview_x;
  uint16_t source_y = preview_y;

  if ((temp14_frame == NULL) || (frame_width == 0U) || (frame_height == 0U))
  {
    return 0U;
  }

  if (source_x >= frame_width)
  {
    source_x = (uint16_t)(frame_width - 1U);
  }

  if (source_y >= frame_height)
  {
    source_y = (uint16_t)(frame_height - 1U);
  }

  /* Mirror/flip is self-inverse, so the same transform maps preview coords back to raw source coords. */
  tiny1c_thermal_app_transform_frame_point(source_x, source_y, &source_x, &source_y);
  return temp14_frame[((uint32_t)source_y * (uint32_t)frame_width) + (uint32_t)source_x];
}

/**
  * @brief  Report whether one preview-oriented pixel lies inside the known hot-biased top-right region.
  * @param  x Preview-oriented X coordinate.
  * @param  y Preview-oriented Y coordinate.
  * @retval uint8_t 1 when the pixel is inside the biased region, otherwise 0.
  */
static uint8_t app_threadx_thermal_ai_is_topright_bias_region(uint16_t x,
                                                              uint16_t y)
{
#if (CFG_THERMAL_AI_TOPRIGHT_BIAS_COMP_ENABLE == 0U)
  TX_PARAMETER_NOT_USED(x);
  TX_PARAMETER_NOT_USED(y);
  return 0U;
#else
  static const uint16_t s_region_y[] = { 0U, 10U, 16U, 29U, 48U, 74U, 112U, 152U, 191U };
  static const uint16_t s_region_x[] = { 109U, 128U, 154U, 182U, 208U, 230U, 243U, 251U, 256U };
  uint32_t segment_index;

  if ((x >= CFG_THERMAL_AI_INPUT_WIDTH) || (y >= CFG_THERMAL_AI_INPUT_HEIGHT))
  {
    return 0U;
  }

  if (y <= s_region_y[0])
  {
    return (x >= s_region_x[0]) ? 1U : 0U;
  }

  for (segment_index = 1U; segment_index < (sizeof(s_region_y) / sizeof(s_region_y[0])); segment_index++)
  {
    if (y <= s_region_y[segment_index])
    {
      uint32_t y0 = s_region_y[segment_index - 1U];
      uint32_t y1 = s_region_y[segment_index];
      uint32_t x0 = s_region_x[segment_index - 1U];
      uint32_t x1 = s_region_x[segment_index];
      uint32_t boundary_x = x0;

      if (y1 > y0)
      {
        boundary_x = x0 + (((uint32_t)(y - y0) * (x1 - x0)) + ((y1 - y0) / 2U)) / (y1 - y0);
      }

      return ((uint32_t)x >= boundary_x) ? 1U : 0U;
    }
  }

  return 0U;
#endif
}

/**
  * @brief  Set the text of a status badge label.
  * @param  badge Badge container object.
  * @param  text New badge text.
  * @retval None
  */
static void app_threadx_gui_set_badge_text(lv_obj_t *badge, const char *text)
{
  lv_obj_t *label;
  lv_coord_t label_width;

  if ((badge == NULL) || (text == NULL))
  {
    return;
  }

  label = lv_obj_get_child(badge, 0);
  if (label != NULL)
  {
    label_width = lv_obj_get_width(badge) - 12;
    if (label_width < 8)
    {
      label_width = 8;
    }

    lv_label_set_text(label, text);
    lv_obj_set_width(label, label_width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);

    lv_obj_center(label);
  }
}

/**
  * @brief  Format a temperature badge text string.
  * @param  buffer Output buffer.
  * @param  buffer_size Output buffer size.
  * @param  prefix Text prefix.
  * @param  temp_centi_c Temperature in centi-degrees Celsius.
  * @retval None
  */
static void app_threadx_gui_format_temp_text(char *buffer, uint32_t buffer_size, const char *prefix, int32_t temp_centi_c, uint8_t unit_celsius)
{
  int32_t abs_temp_centi;
  int32_t temp_int;
  int32_t temp_frac;
  int32_t display_temp_centi;
  char unit_char;

  if ((buffer == NULL) || (buffer_size == 0U) || (prefix == NULL))
  {
    return;
  }

  display_temp_centi = app_threadx_centi_celsius_to_unit_centi_celsius(temp_centi_c, unit_celsius);
  unit_char = (unit_celsius != 0U) ? 'C' : 'F';

  abs_temp_centi = (display_temp_centi < 0) ? -display_temp_centi : display_temp_centi;
  temp_int = abs_temp_centi / 100;
  temp_frac = abs_temp_centi % 100;
  (void)snprintf(buffer,
                 buffer_size,
                 "%s%s%" PRId32 ".%02" PRId32 " %c",
                 prefix,
                 (display_temp_centi < 0) ? "-" : "",
                 temp_int,
                 temp_frac,
                 unit_char);
}

/**
  * @brief  Format the battery badge text from the latest cached battery state.
  * @param  buffer Output buffer.
  * @param  buffer_size Output buffer size.
  * @retval None
  */
static void app_threadx_gui_format_battery_text(char *buffer, uint32_t buffer_size)
{
  uint8_t battery_percent;
  uint8_t battery_valid;
  uint8_t battery_charge_state;
  const char *state_prefix;

  if ((buffer == NULL) || (buffer_size == 0U))
  {
    return;
  }

  battery_valid = g_battery_cache_valid;
  battery_percent = g_battery_cache_percent;
  battery_charge_state = g_battery_cache_charge_state;
  if (battery_valid == 0U)
  {
    (void)snprintf(buffer, buffer_size, "BAT --%%");
    return;
  }

  switch ((BQ27441_ChargeStatusTypeDef)battery_charge_state)
  {
    case BQ27441_CHARGE_STATUS_CHARGING:
      state_prefix = "CHG";
      break;

    case BQ27441_CHARGE_STATUS_DISCHARGING:
      state_prefix = "DSG";
      break;

    case BQ27441_CHARGE_STATUS_SLEEP:
    default:
      state_prefix = "BAT";
      break;
  }

  (void)snprintf(buffer, buffer_size, "%s %u%%", state_prefix, (unsigned)battery_percent);
}

/**
  * @brief  Format one short temperature-detector status string for the top badge.
  * @param  buffer Output buffer pointer.
  * @param  buffer_size Output buffer size.
  * @retval None
  */
static void app_threadx_gui_format_ai_status_text(char *buffer, uint32_t buffer_size)
{
  thermal_ai_result_t result;
  uint8_t result_valid = 0U;

  if ((buffer == NULL) || (buffer_size == 0U))
  {
    return;
  }

  if ((CFG_THERMAL_AI_ENABLE == 0U) || (app_thermal_ai_is_enabled() == 0U))
  {
    (void)snprintf(buffer, buffer_size, "AI OFF");
    return;
  }

  if (g_thermal_ai_model_ready == 0U)
  {
    (void)snprintf(buffer, buffer_size, "AI ON --");
    return;
  }

  if (tx_mutex_get(&g_thermal_ai_mutex, TX_NO_WAIT) == TX_SUCCESS)
  {
    result = g_thermal_ai_runtime.last_result;
    result_valid = 1U;
    tx_mutex_put(&g_thermal_ai_mutex);
  }

  if ((result_valid != 0U) &&
      (result.detection_count != 0U) &&
      (result.detections[0].valid != 0U))
  {
    const thermal_ai_detection_t *detection = &result.detections[0];

    (void)snprintf(buffer,
                   buffer_size,
                   "AI H%u %u %lums",
                   (unsigned int)result.detection_count,
                   (unsigned int)((detection->confidence_permille + 5U) / 10U),
                   (unsigned long)g_thermal_ai_last_inference_ms);
  }
  else
  {
    (void)snprintf(buffer,
                   buffer_size,
                   "AI ON -- %lums",
                   (unsigned long)g_thermal_ai_last_inference_ms);
  }
}


/**
  * @brief  Refresh the preview panel from the latest thermal frame.
  * @param  ui UI context.
  * @retval None
  */
static uint8_t app_threadx_gui_update_preview(lv_ui *ui)
{
  const uint16_t *temp14_frame;
  thermal_ai_result_t ai_result_snapshot;
  uint8_t mirror_enable;
  uint8_t flip_enable;
  uint16_t frame_width;
  uint16_t frame_height;
  uint32_t pixel_count;
  uint32_t pixel_index;
  uint16_t min_temp14;
  uint16_t max_temp14;
  uint16_t center_temp14;
  uint16_t min_temp_x = 0U;
  uint16_t min_temp_y = 0U;
  uint16_t max_temp_x = 0U;
  uint16_t max_temp_y = 0U;
  int32_t center_temp_centi_c;
  uint8_t unit_celsius;
  uint8_t extrema_cache_valid;
  uint8_t ai_result_valid = 0U;
  uint8_t ai_hide_unconfirmed_abnormal = 0U;
  uint8_t abnormal_detected = 0U;

  if (ui == NULL)
  {
    return 0U;
  }

  temp14_frame = tiny1c_thermal_app_get_temp14_frame();
  if (temp14_frame == NULL)
  {
    return 0U;
  }

  frame_width = tiny1c_thermal_app_get_frame_width();
  frame_height = tiny1c_thermal_app_get_frame_height();
  mirror_enable = tiny1c_thermal_app_get_preview_mirror_enabled();
  flip_enable = tiny1c_thermal_app_get_preview_flip_enabled();
  unit_celsius = thermal_gui_is_unit_celsius();
  pixel_count = (uint32_t)frame_width * (uint32_t)frame_height;
  min_temp14 = 0xFFFFU;
  max_temp14 = 0U;
  extrema_cache_valid = g_extrema_cache_valid;
  if ((extrema_cache_valid != 0U) &&
      (g_extrema_cache_max_temp_x < frame_width) &&
      (g_extrema_cache_max_temp_y < frame_height) &&
      (g_extrema_cache_min_temp_x < frame_width) &&
      (g_extrema_cache_min_temp_y < frame_height))
  {
    max_temp14 = g_extrema_cache_max_temp14;
    min_temp14 = g_extrema_cache_min_temp14;
    max_temp_x = g_extrema_cache_max_temp_x;
    max_temp_y = g_extrema_cache_max_temp_y;
    min_temp_x = g_extrema_cache_min_temp_x;
    min_temp_y = g_extrema_cache_min_temp_y;
  }
  else
  {
    for (pixel_index = 0U; pixel_index < pixel_count; pixel_index++)
    {
      uint16_t pixel_temp14 = temp14_frame[pixel_index];

      if (pixel_temp14 < min_temp14)
      {
        min_temp14 = pixel_temp14;
        min_temp_x = (uint16_t)(pixel_index % frame_width);
        min_temp_y = (uint16_t)(pixel_index / frame_width);
      }
      if (pixel_temp14 > max_temp14)
      {
        max_temp14 = pixel_temp14;
        max_temp_x = (uint16_t)(pixel_index % frame_width);
        max_temp_y = (uint16_t)(pixel_index / frame_width);
      }
    }
  }

  center_temp14 = temp14_frame[((uint32_t)frame_height / 2U) * (uint32_t)frame_width + ((uint32_t)frame_width / 2U)];
  center_temp_centi_c = tiny1c_thermal_app_get_center_temp_centi_c();
  if (center_temp_centi_c == 0)
  {
    center_temp_centi_c = app_threadx_temp14_to_compensated_centi_celsius(center_temp14);
  }

  app_threadx_gui_update_preview_layer(ui->WidgetsDemo_preview_img,
                                       ui->WidgetsDemo_preview_center_temp,
                                       ui->WidgetsDemo_preview_max_temp,
                                       ui->WidgetsDemo_preview_min_temp,
                                       ui->WidgetsDemo_preview_max_marker,
                                       ui->WidgetsDemo_preview_min_marker,
                                       CFG_GUI_PREVIEW_WIDTH,
                                       CFG_GUI_PREVIEW_HEIGHT,
                                       frame_width,
                                       frame_height,
                                       mirror_enable,
                                       flip_enable,
                                       unit_celsius,
                                       min_temp14,
                                       max_temp14,
                                       center_temp_centi_c,
                                       min_temp_x,
                                       min_temp_y,
                                       max_temp_x,
                                       max_temp_y);
  app_threadx_gui_update_preview_layer(ui->WidgetsDemo_fullscreen_preview_img,
                                       ui->WidgetsDemo_fullscreen_preview_center_temp,
                                       ui->WidgetsDemo_fullscreen_preview_max_temp,
                                       ui->WidgetsDemo_fullscreen_preview_min_temp,
                                       ui->WidgetsDemo_fullscreen_preview_max_marker,
                                       ui->WidgetsDemo_fullscreen_preview_min_marker,
                                       CFG_GUI_FULLSCREEN_WIDTH,
                                       CFG_GUI_FULLSCREEN_HEIGHT,
                                       frame_width,
                                       frame_height,
                                       mirror_enable,
                                       flip_enable,
                                       unit_celsius,
                                       min_temp14,
                                       max_temp14,
                                       center_temp_centi_c,
                                       min_temp_x,
                                       min_temp_y,
                                       max_temp_x,
                                       max_temp_y);

  if ((CFG_THERMAL_AI_ENABLE != 0U) &&
      (app_thermal_ai_is_enabled() != 0U) &&
      (tx_mutex_get(&g_thermal_ai_mutex, TX_NO_WAIT) == TX_SUCCESS))
  {
    if (g_thermal_ai_model_ready != 0U)
    {
      ai_result_snapshot = g_thermal_ai_runtime.last_result;
      ai_result_valid = 1U;
    }
    tx_mutex_put(&g_thermal_ai_mutex);
  }

  if (CFG_THERMAL_AI_ENABLE != 0U)
  {
    if ((app_thermal_ai_is_enabled() != 0U) &&
        (ai_result_valid != 0U) &&
        (thermal_ai_runtime_result_has_class(&ai_result_snapshot,
                                             (uint8_t)THERMAL_AI_CLASS_CIRCUIT_BOARD_ABNORMAL_HOTSPOT,
                                             1U) != 0U) &&
        (ai_hide_unconfirmed_abnormal == 0U))
    {
      abnormal_detected = 1U;
    }

  }

  return abnormal_detected;
}

/**
  * @brief  Update one preview layer and its overlays.
  * @param  preview_img Preview image object.
  * @param  preview_center_temp Center-temperature badge.
  * @param  preview_max_temp Maximum-temperature badge.
  * @param  preview_min_temp Minimum-temperature badge.
  * @param  preview_max_marker Maximum marker label.
  * @param  preview_min_marker Minimum marker label.
  * @param  preview_width Rendered preview width in pixels.
  * @param  preview_height Rendered preview height in pixels.
  * @param  frame_width Temperature-frame width.
  * @param  frame_height Temperature-frame height.
  * @param  mirror_enable Mirror flag.
  * @param  flip_enable Flip flag.
  * @param  unit_celsius Unit selector flag.
  * @param  min_temp14 Minimum temperature sample.
  * @param  max_temp14 Maximum temperature sample.
  * @param  center_temp_centi_c Center temperature in centi-degrees Celsius.
  * @param  min_temp_x Minimum-sample X coordinate.
  * @param  min_temp_y Minimum-sample Y coordinate.
  * @param  max_temp_x Maximum-sample X coordinate.
  * @param  max_temp_y Maximum-sample Y coordinate.
  * @retval None
  */
static void app_threadx_gui_update_preview_layer(lv_obj_t *preview_img,
                                                 lv_obj_t *preview_center_temp,
                                                 lv_obj_t *preview_max_temp,
                                                 lv_obj_t *preview_min_temp,
                                                 lv_obj_t *preview_max_marker,
                                                 lv_obj_t *preview_min_marker,
                                                 uint16_t preview_width,
                                                 uint16_t preview_height,
                                                 uint16_t frame_width,
                                                 uint16_t frame_height,
                                                 uint8_t mirror_enable,
                                                 uint8_t flip_enable,
                                                 uint8_t unit_celsius,
                                                 uint16_t min_temp14,
                                                 uint16_t max_temp14,
                                                 int32_t center_temp_centi_c,
                                                 uint16_t min_temp_x,
                                                 uint16_t min_temp_y,
                                                 uint16_t max_temp_x,
                                                 uint16_t max_temp_y)
{
  int32_t image_x;
  int32_t image_y;
  int32_t max_disp_x;
  int32_t max_disp_y;
  int32_t min_disp_x;
  int32_t min_disp_y;
  int32_t max_label_x;
  int32_t max_label_y;
  int32_t min_label_x;
  int32_t min_label_y;
  char temp_text[32];

  if ((preview_img == NULL) || (preview_center_temp == NULL) || (preview_max_temp == NULL) || (preview_min_temp == NULL))
  {
    return;
  }

  app_threadx_gui_format_temp_text(temp_text, sizeof(temp_text), "Center ", center_temp_centi_c, unit_celsius);
  app_threadx_gui_set_badge_text(preview_center_temp, temp_text);

  app_threadx_gui_format_temp_text(temp_text, sizeof(temp_text), "Max ", app_threadx_temp14_to_compensated_centi_celsius(max_temp14), unit_celsius);
  app_threadx_gui_set_badge_text(preview_max_temp, temp_text);

  app_threadx_gui_format_temp_text(temp_text, sizeof(temp_text), "Min ", app_threadx_temp14_to_compensated_centi_celsius(min_temp14), unit_celsius);
  app_threadx_gui_set_badge_text(preview_min_temp, temp_text);

  if ((preview_max_marker == NULL) || (preview_min_marker == NULL))
  {
    return;
  }

  image_x = lv_obj_get_x(preview_img);
  image_y = lv_obj_get_y(preview_img);

  (void)mirror_enable;
  (void)flip_enable;
  tiny1c_thermal_app_transform_frame_point(max_temp_x, max_temp_y, &max_temp_x, &max_temp_y);
  tiny1c_thermal_app_transform_frame_point(min_temp_x, min_temp_y, &min_temp_x, &min_temp_y);
  max_disp_x = (int32_t)max_temp_x;
  max_disp_y = (int32_t)max_temp_y;
  min_disp_x = (int32_t)min_temp_x;
  min_disp_y = (int32_t)min_temp_y;

  max_label_x = image_x + (max_disp_x * (int32_t)preview_width) / (int32_t)frame_width + 4;
  max_label_y = image_y + (max_disp_y * (int32_t)preview_height) / (int32_t)frame_height - 12;
  min_label_x = image_x + (min_disp_x * (int32_t)preview_width) / (int32_t)frame_width + 4;
  min_label_y = image_y + (min_disp_y * (int32_t)preview_height) / (int32_t)frame_height - 12;

  if (max_label_x < image_x)
  {
    max_label_x = image_x;
  }
  if (max_label_y < image_y)
  {
    max_label_y = image_y;
  }
  if (min_label_x < image_x)
  {
    min_label_x = image_x;
  }
  if (min_label_y < image_y)
  {
    min_label_y = image_y;
  }
  if (max_label_x > (image_x + (int32_t)preview_width - 18))
  {
    max_label_x = image_x + (int32_t)preview_width - 18;
  }
  if (max_label_y > (image_y + (int32_t)preview_height - 18))
  {
    max_label_y = image_y + (int32_t)preview_height - 18;
  }
  if (min_label_x > (image_x + (int32_t)preview_width - 18))
  {
    min_label_x = image_x + (int32_t)preview_width - 18;
  }
  if (min_label_y > (image_y + (int32_t)preview_height - 18))
  {
    min_label_y = image_y + (int32_t)preview_height - 18;
  }

  lv_obj_set_pos(preview_max_marker, (lv_coord_t)max_label_x, (lv_coord_t)max_label_y);
  lv_obj_set_pos(preview_min_marker, (lv_coord_t)min_label_x, (lv_coord_t)min_label_y);
}

/**
  * @brief  Expand an RGB565 frame by an exact 4:5 ratio without per-pixel division.
  * @param  source_frame Source RGB565 frame.
  * @param  dest_frame Destination RGB565 frame.
  * @param  source_width Source frame width; must be a multiple of four.
  * @param  source_height Source frame height; must be a multiple of four.
  * @param  dest_width Destination frame width; must equal source width times 5/4.
  * @retval None
  */
static void app_threadx_gui_expand_frame_4_to_5(const uint16_t *source_frame,
                                                uint16_t *dest_frame,
                                                uint16_t source_width,
                                                uint16_t source_height,
                                                uint16_t dest_width)
{
  uint32_t source_y;
  uint32_t dest_y = 0U;

  for (source_y = 0U; source_y < source_height; source_y++)
  {
    const uint16_t *source_row = &source_frame[source_y * (uint32_t)source_width];
    uint16_t *dest_row = &dest_frame[dest_y * (uint32_t)dest_width];
    uint32_t source_x;
    uint32_t dest_x = 0U;

    for (source_x = 0U; source_x < source_width; source_x += 4U)
    {
      dest_row[dest_x] = source_row[source_x];
      dest_row[dest_x + 1U] = source_row[source_x + 1U];
      dest_row[dest_x + 2U] = source_row[source_x + 2U];
      dest_row[dest_x + 3U] = source_row[source_x + 2U];
      dest_row[dest_x + 4U] = source_row[source_x + 3U];
      dest_x += 5U;
    }

    dest_y++;
    if ((source_y & 3U) == 2U)
    {
      memcpy(&dest_frame[dest_y * (uint32_t)dest_width],
             dest_row,
             (size_t)dest_width * sizeof(uint16_t));
      dest_y++;
    }
  }
}

/**
  * @brief  Scale one RGB565 frame into a fixed GUI image buffer.
  * @param  source_frame Source RGB565 frame.
  * @param  dest_frame Destination RGB565 frame.
  * @param  source_width Source frame width.
  * @param  source_height Source frame height.
  * @param  dest_width Destination frame width.
  * @param  dest_height Destination frame height.
  * @retval None
  */
static void app_threadx_gui_build_scaled_frame(const uint16_t *source_frame,
                                               uint16_t *dest_frame,
                                               uint16_t source_width,
                                               uint16_t source_height,
                                               uint16_t dest_width,
                                               uint16_t dest_height)
{
  uint32_t dest_y;
  uint32_t dest_x;
  uint32_t source_x_step_q16;
  uint32_t source_y_step_q16;
  uint32_t source_y_q16;

  if ((source_frame == NULL) || (dest_frame == NULL) ||
      (source_width == 0U) || (source_height == 0U) ||
      (dest_width == 0U) || (dest_height == 0U))
  {
    return;
  }

  if ((source_width == dest_width) && (source_height == dest_height))
  {
    memcpy(dest_frame,
           source_frame,
           (size_t)source_width * (size_t)source_height * sizeof(uint16_t));
    return;
  }

  if (((source_width & 3U) == 0U) &&
      ((source_height & 3U) == 0U) &&
      (((uint32_t)source_width * 5U) == ((uint32_t)dest_width * 4U)) &&
      (((uint32_t)source_height * 5U) == ((uint32_t)dest_height * 4U)))
  {
    app_threadx_gui_expand_frame_4_to_5(source_frame,
                                        dest_frame,
                                        source_width,
                                        source_height,
                                        dest_width);
    return;
  }

  source_x_step_q16 = ((uint32_t)source_width << 16) / dest_width;
  source_y_step_q16 = ((uint32_t)source_height << 16) / dest_height;
  source_y_q16 = source_y_step_q16 / 2U;

  for (dest_y = 0U; dest_y < dest_height; dest_y++)
  {
    uint32_t src_y = source_y_q16 >> 16;
    uint32_t dest_row_index = dest_y * (uint32_t)dest_width;
    uint32_t src_row_index;
    uint32_t source_x_q16 = source_x_step_q16 / 2U;

    if (src_y >= source_height)
    {
      src_y = (uint32_t)source_height - 1U;
    }
    src_row_index = src_y * (uint32_t)source_width;

    for (dest_x = 0U; dest_x < dest_width; dest_x++)
    {
      uint32_t src_x = source_x_q16 >> 16;
      if (src_x >= source_width)
      {
        src_x = (uint32_t)source_width - 1U;
      }
      dest_frame[dest_row_index + dest_x] = source_frame[src_row_index + src_x];
      source_x_q16 += source_x_step_q16;
    }
    source_y_q16 += source_y_step_q16;
  }
}

/**
  * @brief  Select the camera image shown by the GUI.
  * @param  mode Thermal, visible-light, or fusion view.
  * @retval None
  */
void app_camera_view_set_mode(app_camera_view_mode_t mode)
{
  if (mode > APP_CAMERA_VIEW_FUSION)
  {
    mode = APP_CAMERA_VIEW_THERMAL;
  }

  if (g_camera_view_mode != mode)
  {
    g_camera_view_mode = mode;
    g_camera_render_generation++;
    __DMB();
  }
}

/**
  * @brief  Read the active camera view mode.
  * @retval app_camera_view_mode_t Current GUI view mode.
  */
app_camera_view_mode_t app_camera_view_get_mode(void)
{
  return g_camera_view_mode;
}

/**
  * @brief  Copy the current visible-light alignment parameters.
  * @param  alignment Destination parameter structure.
  * @retval None
  */
void app_camera_alignment_get(app_camera_alignment_t *alignment)
{
  if (alignment == NULL)
  {
    return;
  }

  alignment->offset_x = g_camera_alignment.offset_x;
  alignment->offset_y = g_camera_alignment.offset_y;
  alignment->scale_permille = g_camera_alignment.scale_permille;
  alignment->rotation_degrees = g_camera_alignment.rotation_degrees;
  alignment->transform_flags = g_camera_alignment.transform_flags;
  alignment->visible_alpha = g_camera_alignment.visible_alpha;
}

/**
  * @brief  Apply bounded incremental alignment changes to the visible layer.
  * @param  delta_x Horizontal movement in 512x384 canvas pixels.
  * @param  delta_y Vertical movement in 512x384 canvas pixels.
  * @param  delta_scale_permille Scale change in one-thousandths.
  * @param  delta_rotation_degrees Clockwise rotation change in degrees.
  * @param  delta_alpha Visible-layer alpha change.
  * @retval None
  */
void app_camera_alignment_adjust(int16_t delta_x,
                                 int16_t delta_y,
                                 int16_t delta_scale_permille,
                                 int8_t delta_rotation_degrees,
                                 int16_t delta_alpha)
{
  int32_t offset_x = (int32_t)g_camera_alignment.offset_x + delta_x;
  int32_t offset_y = (int32_t)g_camera_alignment.offset_y + delta_y;
  int32_t scale = (int32_t)g_camera_alignment.scale_permille + delta_scale_permille;
  int32_t rotation = (int32_t)g_camera_alignment.rotation_degrees + delta_rotation_degrees;
  int32_t alpha = (int32_t)g_camera_alignment.visible_alpha + delta_alpha;

  if (offset_x < CFG_CAMERA_ALIGNMENT_OFFSET_MIN_X) offset_x = CFG_CAMERA_ALIGNMENT_OFFSET_MIN_X;
  if (offset_x > CFG_CAMERA_ALIGNMENT_OFFSET_MAX_X) offset_x = CFG_CAMERA_ALIGNMENT_OFFSET_MAX_X;
  if (offset_y < CFG_CAMERA_ALIGNMENT_OFFSET_MIN_Y) offset_y = CFG_CAMERA_ALIGNMENT_OFFSET_MIN_Y;
  if (offset_y > CFG_CAMERA_ALIGNMENT_OFFSET_MAX_Y) offset_y = CFG_CAMERA_ALIGNMENT_OFFSET_MAX_Y;
  if (scale < (int32_t)CFG_CAMERA_ALIGNMENT_SCALE_MIN) scale = CFG_CAMERA_ALIGNMENT_SCALE_MIN;
  if (scale > (int32_t)CFG_CAMERA_ALIGNMENT_SCALE_MAX) scale = CFG_CAMERA_ALIGNMENT_SCALE_MAX;
  if (rotation < CFG_CAMERA_ALIGNMENT_ROTATION_MIN) rotation = CFG_CAMERA_ALIGNMENT_ROTATION_MIN;
  if (rotation > CFG_CAMERA_ALIGNMENT_ROTATION_MAX) rotation = CFG_CAMERA_ALIGNMENT_ROTATION_MAX;
  if (alpha < 0) alpha = 0;
  if (alpha > 255) alpha = 255;

  g_camera_alignment.offset_x = (int16_t)offset_x;
  g_camera_alignment.offset_y = (int16_t)offset_y;
  g_camera_alignment.scale_permille = (uint16_t)scale;
  g_camera_alignment.rotation_degrees = (int8_t)rotation;
  g_camera_alignment.visible_alpha = (uint8_t)alpha;
  g_camera_render_generation++;
  __DMB();
}

/**
  * @brief  Toggle one visible-light mirror or flip flag.
  * @param  transform_flag APP_CAMERA_ALIGNMENT_MIRROR or APP_CAMERA_ALIGNMENT_FLIP.
  * @retval None
  */
void app_camera_alignment_toggle(uint8_t transform_flag)
{
  g_camera_alignment.transform_flags ^= transform_flag &
                                          (APP_CAMERA_ALIGNMENT_MIRROR | APP_CAMERA_ALIGNMENT_FLIP);
  g_camera_render_generation++;
  __DMB();
}

/**
  * @brief  Restore neutral visible-light alignment and 50 percent alpha.
  * @retval None
  */
void app_camera_alignment_reset(void)
{
  g_camera_alignment.offset_x = 0;
  g_camera_alignment.offset_y = 0;
  g_camera_alignment.scale_permille = CFG_CAMERA_ALIGNMENT_DEFAULT_SCALE;
  g_camera_alignment.rotation_degrees = 0;
  g_camera_alignment.transform_flags = 0U;
  g_camera_alignment.visible_alpha = CFG_CAMERA_ALIGNMENT_DEFAULT_ALPHA;
  g_camera_render_generation++;
  __DMB();
}

/**
  * @brief  Read a coherent snapshot of the latest completed IMX219 frame metadata.
  * @param  snapshot Destination snapshot.
  * @retval uint8_t 1 when a valid stable frame was captured, otherwise 0.
  */
static uint8_t app_threadx_imx219_get_frame_snapshot(app_imx219_frame_snapshot_t *snapshot)
{
  uint32_t attempt;

  if (snapshot == NULL)
  {
    return 0U;
  }

  for (attempt = 0U; attempt < 3U; attempt++)
  {
    uint32_t counter = g_imx219_runtime_frame_counter;
    uintptr_t frame_address;
    uint32_t timestamp_ms;

    __DMB();
    frame_address = g_imx219_completed_frame_address;
    timestamp_ms = g_imx219_last_frame_timestamp_ms;
    __DMB();
    if (counter != g_imx219_runtime_frame_counter)
    {
      continue;
    }
    if (((frame_address == (uintptr_t)g_imx219_rgb565_frame_a) ||
         (frame_address == (uintptr_t)g_imx219_rgb565_frame_b)) &&
        (timestamp_ms != 0U))
    {
      snapshot->frame = (const uint16_t *)frame_address;
      snapshot->counter = counter;
      snapshot->timestamp_ms = timestamp_ms;
      return 1U;
    }
    break;
  }

  return 0U;
}

/**
  * @brief  Return the shortest distance between two wrapping HAL ticks.
  * @param  tick_a First millisecond tick.
  * @param  tick_b Second millisecond tick.
  * @retval uint32_t Absolute tick distance.
  */
static uint32_t app_threadx_tick_abs_delta(uint32_t tick_a, uint32_t tick_b)
{
  uint32_t forward_delta = tick_a - tick_b;
  uint32_t backward_delta = tick_b - tick_a;

  return (forward_delta < backward_delta) ? forward_delta : backward_delta;
}

/**
  * @brief  Transform the visible frame into the thermal 512x384 coordinate system.
  * @retval uint8_t 1 when a visible frame was available, otherwise 0.
  */
static uint8_t app_threadx_gui_build_camera_canvas(void)
{
  static const int16_t sin_q14[11] = {0, 286, 572, 857, 1143, 1428, 1713, 1997, 2280, 2563, 2845};
  static const int16_t cos_q14[11] = {16384, 16382, 16374, 16362, 16344, 16322, 16294, 16260, 16225, 16182, 16135};
  app_imx219_frame_snapshot_t visible_snapshot;
  const uint16_t *thermal_frame = tiny1c_thermal_app_get_rgb565_frame();
  app_camera_alignment_t alignment;
  app_camera_view_mode_t mode = g_camera_view_mode;
  uint32_t thermal_sequence;
  int32_t rotation_index;
  int32_t sin_value_q14;
  int32_t cos_value_q14;
  int32_t inv_scale_q16;
  int32_t source_m00_q16;
  int32_t source_m01_q16;
  int32_t source_m10_q16;
  int32_t source_m11_q16;
  int32_t row_source_x_q16;
  int32_t row_source_y_q16;
  uint32_t visible_weight;
  uint32_t thermal_weight;
  uint32_t frame_skew_ms;
  uint32_t x;
  uint32_t y;

  app_camera_alignment_get(&alignment);
  visible_weight = alignment.visible_alpha;
  if (visible_weight == 255U)
  {
    visible_weight = 256U;
  }
  thermal_weight = 256U - visible_weight;
  if (app_threadx_imx219_get_frame_snapshot(&visible_snapshot) == 0U)
  {
    if ((mode == APP_CAMERA_VIEW_FUSION) && (thermal_frame != NULL))
    {
      thermal_sequence = tiny1c_thermal_app_get_frame_sequence();
      if ((thermal_sequence & 1U) != 0U)
      {
        return 0U;
      }
      memcpy(g_gui_camera_canvas_rgb565,
             thermal_frame,
             sizeof(g_gui_camera_canvas_rgb565));
      __DMB();
      if (thermal_sequence != tiny1c_thermal_app_get_frame_sequence())
      {
        return 0U;
      }
      return 1U;
    }
    memset(g_gui_camera_canvas_rgb565, 0, sizeof(g_gui_camera_canvas_rgb565));
    return 0U;
  }

  app_threadx_dcache_invalidate_by_addr((void *)visible_snapshot.frame, CFG_IMX219_FRAME_BYTES);

  if ((mode == APP_CAMERA_VIEW_FUSION) && (thermal_frame != NULL))
  {
    thermal_sequence = tiny1c_thermal_app_get_frame_sequence();
    if ((thermal_sequence & 1U) != 0U)
    {
      return 0U;
    }
    memcpy(g_gui_camera_canvas_rgb565,
           thermal_frame,
           sizeof(g_gui_camera_canvas_rgb565));
    __DMB();
    if (thermal_sequence != tiny1c_thermal_app_get_frame_sequence())
    {
      return 0U;
    }
    frame_skew_ms = app_threadx_tick_abs_delta(
        visible_snapshot.timestamp_ms,
        tiny1c_thermal_app_get_frame_timestamp_ms());
    g_camera_sync_last_skew_ms = frame_skew_ms;
    if (frame_skew_ms > g_camera_sync_max_skew_ms)
    {
      g_camera_sync_max_skew_ms = frame_skew_ms;
    }
    if (frame_skew_ms > CFG_CAMERA_SYNC_MAX_SKEW_MS)
    {
      g_camera_sync_wait_count++;
      return 0U;
    }
    if (alignment.visible_alpha == 0U)
    {
      return 1U;
    }
  }
  else
  {
    memset(g_gui_camera_canvas_rgb565, 0, sizeof(g_gui_camera_canvas_rgb565));
  }

  rotation_index = alignment.rotation_degrees;
  if (rotation_index < 0)
  {
    rotation_index = -rotation_index;
  }
  sin_value_q14 = sin_q14[rotation_index];
  if (alignment.rotation_degrees < 0)
  {
    sin_value_q14 = -sin_value_q14;
  }
  cos_value_q14 = cos_q14[rotation_index];
  inv_scale_q16 = (int32_t)(((int64_t)1000 << 16) / alignment.scale_permille);

  source_m00_q16 = (int32_t)((((int64_t)cos_value_q14 * inv_scale_q16) >> 14) *
                              CFG_IMX219_FRAME_WIDTH / CFG_CAMERA_CANVAS_WIDTH);
  source_m01_q16 = (int32_t)((((int64_t)sin_value_q14 * inv_scale_q16) >> 14) *
                              CFG_IMX219_FRAME_WIDTH / CFG_CAMERA_CANVAS_WIDTH);
  source_m10_q16 = (int32_t)((((int64_t)-sin_value_q14 * inv_scale_q16) >> 14) *
                              CFG_IMX219_FRAME_HEIGHT / CFG_CAMERA_CANVAS_HEIGHT);
  source_m11_q16 = (int32_t)((((int64_t)cos_value_q14 * inv_scale_q16) >> 14) *
                              CFG_IMX219_FRAME_HEIGHT / CFG_CAMERA_CANVAS_HEIGHT);

  row_source_x_q16 = (int32_t)(((int64_t)(CFG_IMX219_FRAME_WIDTH / 2U) << 16) +
                               (int64_t)source_m00_q16 *
                                 (-alignment.offset_x - (int32_t)(CFG_CAMERA_CANVAS_WIDTH / 2U)) +
                               (int64_t)source_m01_q16 *
                                 (-alignment.offset_y - (int32_t)(CFG_CAMERA_CANVAS_HEIGHT / 2U)));
  row_source_y_q16 = (int32_t)(((int64_t)(CFG_IMX219_FRAME_HEIGHT / 2U) << 16) +
                               (int64_t)source_m10_q16 *
                                 (-alignment.offset_x - (int32_t)(CFG_CAMERA_CANVAS_WIDTH / 2U)) +
                               (int64_t)source_m11_q16 *
                                 (-alignment.offset_y - (int32_t)(CFG_CAMERA_CANVAS_HEIGHT / 2U)));

  for (y = 0U; y < CFG_CAMERA_CANVAS_HEIGHT; y++)
  {
    int32_t source_x_q16 = row_source_x_q16;
    int32_t source_y_q16 = row_source_y_q16;
    uint32_t canvas_row = y * CFG_CAMERA_CANVAS_WIDTH;

    for (x = 0U; x < CFG_CAMERA_CANVAS_WIDTH; x++)
    {
      uint32_t canvas_index = canvas_row + x;
      if ((source_x_q16 >= 0) &&
          (source_x_q16 < (int32_t)(CFG_IMX219_FRAME_WIDTH << 16)) &&
          (source_y_q16 >= 0) &&
          (source_y_q16 < (int32_t)(CFG_IMX219_FRAME_HEIGHT << 16)))
      {
        uint32_t source_x = (uint32_t)source_x_q16 >> 16;
        uint32_t source_y = (uint32_t)source_y_q16 >> 16;
        uint16_t visible_pixel;

        if ((alignment.transform_flags & APP_CAMERA_ALIGNMENT_MIRROR) != 0U)
        {
          source_x = CFG_IMX219_FRAME_WIDTH - 1U - source_x;
        }
        if ((alignment.transform_flags & APP_CAMERA_ALIGNMENT_FLIP) != 0U)
        {
          source_y = CFG_IMX219_FRAME_HEIGHT - 1U - source_y;
        }
        visible_pixel = visible_snapshot.frame[source_y * CFG_IMX219_FRAME_WIDTH + source_x];

        if (mode == APP_CAMERA_VIEW_FUSION)
        {
          uint16_t thermal_pixel = g_gui_camera_canvas_rgb565[canvas_index];
          uint32_t red;
          uint32_t green;
          uint32_t blue;

          red = ((((uint32_t)(visible_pixel >> 11) & 0x1FU) * visible_weight) +
                 (((uint32_t)(thermal_pixel >> 11) & 0x1FU) * thermal_weight)) >> 8;
          green = ((((uint32_t)(visible_pixel >> 5) & 0x3FU) * visible_weight) +
                   (((uint32_t)(thermal_pixel >> 5) & 0x3FU) * thermal_weight)) >> 8;
          blue = ((((uint32_t)visible_pixel & 0x1FU) * visible_weight) +
                  (((uint32_t)thermal_pixel & 0x1FU) * thermal_weight)) >> 8;
          g_gui_camera_canvas_rgb565[canvas_index] = (uint16_t)((red << 11) | (green << 5) | blue);
        }
        else
        {
          g_gui_camera_canvas_rgb565[canvas_index] = visible_pixel;
        }
      }

      source_x_q16 += source_m00_q16;
      source_y_q16 += source_m10_q16;
    }

    row_source_x_q16 += source_m01_q16;
    row_source_y_q16 += source_m11_q16;
  }

  __DMB();
  if ((g_imx219_runtime_frame_counter - visible_snapshot.counter) >= 2U)
  {
    return 0U;
  }

  return 1U;
}

/**
  * @brief  Render the selected camera view into a GUI image buffer.
  * @param  dest_frame Destination RGB565 frame.
  * @param  dest_width Destination width.
  * @param  dest_height Destination height.
  * @retval uint8_t 1 when a stable frame was rendered, otherwise 0.
  */
static uint8_t app_threadx_gui_render_camera_frame(uint16_t *dest_frame,
                                                  uint16_t dest_width,
                                                  uint16_t dest_height)
{
  const uint16_t *source_frame;
  uint16_t source_width;
  uint16_t source_height;
  uint32_t tick_start = HAL_GetTick();
  uint32_t render_ms;

  if (g_camera_view_mode == APP_CAMERA_VIEW_THERMAL)
  {
    source_frame = tiny1c_thermal_app_get_rgb565_frame();
    source_width = tiny1c_thermal_app_get_preview_width();
    source_height = tiny1c_thermal_app_get_preview_height();
  }
  else
  {
    if (app_threadx_gui_build_camera_canvas() == 0U)
    {
      return 0U;
    }
    source_frame = g_gui_camera_canvas_rgb565;
    source_width = CFG_CAMERA_CANVAS_WIDTH;
    source_height = CFG_CAMERA_CANVAS_HEIGHT;
  }

  app_threadx_gui_build_scaled_frame(source_frame,
                                     dest_frame,
                                     source_width,
                                     source_height,
                                     dest_width,
                                     dest_height);
  render_ms = HAL_GetTick() - tick_start;
  g_camera_render_last_ms = render_ms;
  if (render_ms > g_camera_render_max_ms)
  {
    g_camera_render_max_ms = render_ms;
  }
  return 1U;
}
/**
  * @brief  Return one percentile value from the current temp14 histogram.
  * @param  pixel_count Total number of pixels accumulated in the histogram.
  * @retval uint16_t Percentile temp14 value.
  */
static uint16_t app_threadx_thermal_ai_percentile_from_histogram(uint32_t pixel_count)
{
  uint32_t cumulative = 0U;
  uint32_t rank;
  uint32_t histogram_index;

  if (pixel_count == 0U)
  {
    return 0U;
  }

  rank = ((pixel_count - 1U) * CFG_THERMAL_AI_BACKGROUND_PERCENT_NUM) / CFG_THERMAL_AI_BACKGROUND_PERCENT_DEN;
  for (histogram_index = 0U; histogram_index < CFG_THERMAL_AI_HISTOGRAM_BINS; histogram_index++)
  {
    cumulative += (uint32_t)g_thermal_ai_histogram[histogram_index];
    if (cumulative > rank)
    {
      return (uint16_t)histogram_index;
    }
  }

  return (uint16_t)(CFG_THERMAL_AI_HISTOGRAM_BINS - 1U);
}

/**
  * @brief  Detect one abnormal hotspot directly from the oriented temp14 frame.
  * @param  result_ptr Runtime result to update.
  * @param  background_temp14 Median/background temperature in kelvin*16.
  * @retval None
  */
static void app_threadx_thermal_ai_detect_temp14_hotspot(thermal_ai_result_t *result_ptr,
                                                         uint16_t background_temp14)
{
  uint32_t pixel_index;
  uint32_t hot_pixel_count = 0U;
  uint16_t threshold_temp14;
  uint16_t max_temp14 = 0U;
  uint16_t max_x = 0U;
  uint16_t max_y = 0U;
  uint16_t x_min = CFG_THERMAL_AI_INPUT_WIDTH;
  uint16_t y_min = CFG_THERMAL_AI_INPUT_HEIGHT;
  uint16_t x_max = 0U;
  uint16_t y_max = 0U;
  uint8_t peak_score = 0U;
  uint32_t box_width;
  uint32_t box_height;

  if ((result_ptr == NULL) ||
      (background_temp14 < CFG_THERMAL_AI_VALID_TEMP14_MIN) ||
      (background_temp14 > CFG_THERMAL_AI_PREVIEW_CONFIRM_VALID_MAX_TEMP14) ||
      (((uint32_t)background_temp14 + CFG_THERMAL_AI_HOTSPOT_FALLBACK_DELTA_TEMP14) >
       CFG_THERMAL_AI_PREVIEW_CONFIRM_VALID_MAX_TEMP14))
  {
    return;
  }

  threshold_temp14 = (uint16_t)(background_temp14 +
                                CFG_THERMAL_AI_HOTSPOT_FALLBACK_DELTA_TEMP14);

  for (pixel_index = 0U; pixel_index < CFG_THERMAL_AI_INPUT_PIXELS; pixel_index++)
  {
    uint16_t temp14_value = g_thermal_ai_oriented_temp14[pixel_index];
    uint16_t x = (uint16_t)(pixel_index % CFG_THERMAL_AI_INPUT_WIDTH);
    uint16_t y = (uint16_t)(pixel_index / CFG_THERMAL_AI_INPUT_WIDTH);
    uint16_t pixel_threshold_temp14 = threshold_temp14;

#if (CFG_THERMAL_AI_TOPRIGHT_BIAS_COMP_ENABLE != 0U)
    if (app_threadx_thermal_ai_is_topright_bias_region(x, y) != 0U)
    {
      uint32_t topright_threshold_temp14 =
          (uint32_t)background_temp14 + CFG_THERMAL_AI_TOPRIGHT_HOTSPOT_DELTA_TEMP14;

      pixel_threshold_temp14 =
          (topright_threshold_temp14 <= CFG_THERMAL_AI_PREVIEW_CONFIRM_VALID_MAX_TEMP14)
            ? (uint16_t)topright_threshold_temp14
            : CFG_THERMAL_AI_PREVIEW_CONFIRM_VALID_MAX_TEMP14;
    }
#endif

    if ((temp14_value < pixel_threshold_temp14) ||
        (temp14_value < CFG_THERMAL_AI_VALID_TEMP14_MIN) ||
        (temp14_value > CFG_THERMAL_AI_PREVIEW_CONFIRM_VALID_MAX_TEMP14))
    {
      continue;
    }

    hot_pixel_count++;
    if (temp14_value > max_temp14)
    {
      max_temp14 = temp14_value;
      max_x = x;
      max_y = y;
    }
    if (x < x_min)
    {
      x_min = x;
    }
    if (y < y_min)
    {
      y_min = y;
    }
    if (x > x_max)
    {
      x_max = x;
    }
    if (y > y_max)
    {
      y_max = y;
    }
  }

  if (hot_pixel_count < CFG_THERMAL_AI_HOTSPOT_FALLBACK_MIN_PIXELS)
  {
    return;
  }

  x_max = (uint16_t)(x_max + 1U);
  y_max = (uint16_t)(y_max + 1U);
  box_width = (uint32_t)x_max - (uint32_t)x_min;
  box_height = (uint32_t)y_max - (uint32_t)y_min;

  if ((box_width * box_height) > (uint32_t)CFG_THERMAL_AI_ABNORMAL_MAX_AREA_PX)
  {
    uint32_t half_size = CFG_THERMAL_AI_HOTSPOT_FALLBACK_BOX_MAX_PX / 2U;

    x_min = (max_x > half_size) ? (uint16_t)(max_x - half_size) : 0U;
    y_min = (max_y > half_size) ? (uint16_t)(max_y - half_size) : 0U;
    x_max = (uint16_t)(x_min + CFG_THERMAL_AI_HOTSPOT_FALLBACK_BOX_MAX_PX);
    y_max = (uint16_t)(y_min + CFG_THERMAL_AI_HOTSPOT_FALLBACK_BOX_MAX_PX);
  }
  else if ((box_width < CFG_THERMAL_AI_HOTSPOT_FALLBACK_BOX_MIN_PX) ||
           (box_height < CFG_THERMAL_AI_HOTSPOT_FALLBACK_BOX_MIN_PX) ||
           ((box_width * box_height) < (uint32_t)CFG_THERMAL_AI_ABNORMAL_MIN_AREA_PX))
  {
    uint32_t half_size = CFG_THERMAL_AI_HOTSPOT_FALLBACK_BOX_MIN_PX / 2U;

    x_min = (max_x > half_size) ? (uint16_t)(max_x - half_size) : 0U;
    y_min = (max_y > half_size) ? (uint16_t)(max_y - half_size) : 0U;
    x_max = (uint16_t)(x_min + CFG_THERMAL_AI_HOTSPOT_FALLBACK_BOX_MIN_PX);
    y_max = (uint16_t)(y_min + CFG_THERMAL_AI_HOTSPOT_FALLBACK_BOX_MIN_PX);
  }

  if (x_max > CFG_THERMAL_AI_INPUT_WIDTH)
  {
    x_max = CFG_THERMAL_AI_INPUT_WIDTH;
    x_min = (x_max > CFG_THERMAL_AI_HOTSPOT_FALLBACK_BOX_MIN_PX)
              ? (uint16_t)(x_max - CFG_THERMAL_AI_HOTSPOT_FALLBACK_BOX_MIN_PX)
              : 0U;
  }
  if (y_max > CFG_THERMAL_AI_INPUT_HEIGHT)
  {
    y_max = CFG_THERMAL_AI_INPUT_HEIGHT;
    y_min = (y_max > CFG_THERMAL_AI_HOTSPOT_FALLBACK_BOX_MIN_PX)
              ? (uint16_t)(y_max - CFG_THERMAL_AI_HOTSPOT_FALLBACK_BOX_MIN_PX)
              : 0U;
  }
  if ((x_max <= x_min) || (y_max <= y_min))
  {
    return;
  }

  if (max_temp14 > background_temp14)
  {
    uint32_t temp_delta_temp14 = (uint32_t)max_temp14 - (uint32_t)background_temp14;

    peak_score = (temp_delta_temp14 >= 320U)
                   ? 255U
                   : (uint8_t)((temp_delta_temp14 * 255U) / 320U);
  }

  result_ptr->detection_count = 1U;
  result_ptr->detections[0].valid = 1U;
  result_ptr->detections[0].class_id =
      (uint8_t)THERMAL_AI_CLASS_CIRCUIT_BOARD_ABNORMAL_HOTSPOT;
  result_ptr->detections[0].confidence_permille =
      app_threadx_thermal_ai_calc_hotspot_confidence(
          hot_pixel_count,
          (uint32_t)x_max - (uint32_t)x_min,
          (uint32_t)y_max - (uint32_t)y_min,
          peak_score);
  result_ptr->detections[0].bbox.x_min = x_min;
  result_ptr->detections[0].bbox.y_min = y_min;
  result_ptr->detections[0].bbox.x_max = x_max;
  result_ptr->detections[0].bbox.y_max = y_max;
}
/**
  * @brief  Clean one address range from D-Cache with 32-byte alignment.
  * @param  buffer_addr Buffer start address.
  * @param  buffer_size Buffer size in bytes.
  * @retval None
  */
static void app_threadx_dcache_clean_by_addr(void *buffer_addr, uint32_t buffer_size)
{
  uintptr_t start_addr;
  uintptr_t end_addr;

  if ((buffer_addr == NULL) || (buffer_size == 0U))
  {
    return;
  }

  start_addr = ((uintptr_t)buffer_addr) & ~((uintptr_t)31U);
  end_addr = (((uintptr_t)buffer_addr + (uintptr_t)buffer_size + (uintptr_t)31U) & ~((uintptr_t)31U));
  SCB_CleanDCache_by_Addr((uint32_t *)start_addr, (int32_t)(end_addr - start_addr));
}

/**
  * @brief  Invalidate one address range from D-Cache with 32-byte alignment.
  * @param  buffer_addr Buffer start address.
  * @param  buffer_size Buffer size in bytes.
  * @retval None
  */
static void app_threadx_dcache_invalidate_by_addr(void *buffer_addr, uint32_t buffer_size)
{
  uintptr_t start_addr;
  uintptr_t end_addr;

  if ((buffer_addr == NULL) || (buffer_size == 0U))
  {
    return;
  }

  start_addr = ((uintptr_t)buffer_addr) & ~((uintptr_t)31U);
  end_addr = (((uintptr_t)buffer_addr + (uintptr_t)buffer_size + (uintptr_t)31U) & ~((uintptr_t)31U));
  SCB_InvalidateDCache_by_Addr((uint32_t *)start_addr, (int32_t)(end_addr - start_addr));
}

/**
  * @brief  Run the active temp14 hotspot detector for one thermal frame.
  * @param  temp14_frame Latest calibrated temperature frame.
  * @param  frame_counter Current thermal frame counter.
  * @retval None
  */
static void app_threadx_thermal_ai_run_inference(const uint16_t *temp14_frame, uint32_t frame_counter)
{
  if ((CFG_THERMAL_AI_ENABLE != 0U) && (app_thermal_ai_is_enabled() != 0U))
  {
    app_threadx_thermal_ai_run_temp14_hotspot(temp14_frame, frame_counter);
  }
}

/**
  * @brief  Detect one abnormal hotspot from the current temp14 frame.
  * @param  temp14_frame Latest calibrated temperature frame.
  * @param  frame_counter Current thermal frame counter.
  * @retval None
  */
static void app_threadx_thermal_ai_run_temp14_hotspot(const uint16_t *temp14_frame, uint32_t frame_counter)
{
  thermal_ai_result_t result;
  uint16_t background_temp14;
  ULONG tick_start;

  (void)memset(&result, 0, sizeof(result));
  result.frame_counter = frame_counter;

  tick_start = HAL_GetTick();
  background_temp14 = app_threadx_thermal_ai_prepare_preview_temp14(temp14_frame);
  app_threadx_thermal_ai_detect_temp14_hotspot(&result, background_temp14);
  g_thermal_ai_last_inference_ms = HAL_GetTick() - tick_start;

  if (tx_mutex_get(&g_thermal_ai_mutex, TX_WAIT_FOREVER) == TX_SUCCESS)
  {
    if (g_thermal_ai_runtime_enable != 0U)
    {
      thermal_ai_runtime_update(&g_thermal_ai_runtime, &result);
      g_thermal_ai_model_ready = 1U;
    }
    else
    {
      thermal_ai_runtime_reset(&g_thermal_ai_runtime);
      g_thermal_ai_model_ready = 0U;
    }
    tx_mutex_put(&g_thermal_ai_mutex);
  }
}

/**
  * @brief  Build the preview-oriented calibrated temp14 frame used for hotspot detection.
  * @param  temp14_frame Latest calibrated sensor-temperature frame.
  * @retval uint16_t Median valid frame temperature in kelvin*16, or zero.
  */
static uint16_t app_threadx_thermal_ai_prepare_preview_temp14(const uint16_t *temp14_frame)
{
  uint32_t pixel_index = 0U;
  uint32_t valid_pixel_count = 0U;
  uint32_t y;
  uint32_t x;

  if (temp14_frame == NULL)
  {
    return 0U;
  }

  (void)memset(g_thermal_ai_histogram, 0, sizeof(g_thermal_ai_histogram));
  for (y = 0U; y < CFG_THERMAL_AI_INPUT_HEIGHT; y++)
  {
    for (x = 0U; x < CFG_THERMAL_AI_INPUT_WIDTH; x++, pixel_index++)
    {
      uint16_t temp14_value = app_threadx_temp14_get_preview_oriented_sample(
          temp14_frame,
          CFG_THERMAL_AI_INPUT_WIDTH,
          CFG_THERMAL_AI_INPUT_HEIGHT,
          (uint16_t)x,
          (uint16_t)y);

      g_thermal_ai_oriented_temp14[pixel_index] = temp14_value;
      if ((temp14_value >= CFG_THERMAL_AI_VALID_TEMP14_MIN) &&
          (temp14_value <= CFG_THERMAL_AI_PREVIEW_CONFIRM_VALID_MAX_TEMP14))
      {
        g_thermal_ai_histogram[temp14_value]++;
        valid_pixel_count++;
      }
    }
  }

  if (valid_pixel_count == 0U)
  {
    return 0U;
  }

  return app_threadx_thermal_ai_percentile_from_histogram(valid_pixel_count);
}

/**
  * @brief  Convert one temp14 hotspot into a bounded display confidence.
  * @param  hot_pixel_count Count of pixels inside the hotspot candidate.
  * @param  box_width Bounding-box width in detector pixels.
  * @param  box_height Bounding-box height in detector pixels.
  * @param  peak_score Temperature contrast mapped to 0..255.
  * @retval uint16_t Confidence in permille.
  */
static uint16_t app_threadx_thermal_ai_calc_hotspot_confidence(uint32_t hot_pixel_count,
                                                               uint32_t box_width,
                                                               uint32_t box_height,
                                                               uint8_t peak_score)
{
  uint32_t box_area;
  uint32_t fill_permille;
  uint32_t area_term;
  uint32_t confidence_permille;

  box_area = box_width * box_height;
  if (box_area == 0U)
  {
    return CFG_THERMAL_AI_HOTSPOT_FALLBACK_CONFIDENCE_MIN_PERMILLE;
  }

  fill_permille = (hot_pixel_count >= box_area)
                    ? 1000U
                    : ((hot_pixel_count * 1000U) / box_area);
  area_term = hot_pixel_count / 32U;
  if (area_term > 50U)
  {
    area_term = 50U;
  }

  confidence_permille = 400U +
                        (((peak_score > 180U) ? 180U : (uint32_t)peak_score) * 2U) +
                        (fill_permille / 8U) +
                        area_term;

  if (confidence_permille < CFG_THERMAL_AI_HOTSPOT_FALLBACK_CONFIDENCE_MIN_PERMILLE)
  {
    confidence_permille = CFG_THERMAL_AI_HOTSPOT_FALLBACK_CONFIDENCE_MIN_PERMILLE;
  }
  if (confidence_permille > CFG_THERMAL_AI_HOTSPOT_FALLBACK_CONFIDENCE_MAX_PERMILLE)
  {
    confidence_permille = CFG_THERMAL_AI_HOTSPOT_FALLBACK_CONFIDENCE_MAX_PERMILLE;
  }

  return (uint16_t)confidence_permille;
}


/**
  * @brief  Copy the latest enabled hotspot result for GUI-frame composition.
  * @param  result_ptr Output result snapshot.
  * @retval uint8_t 1 when a valid runtime result was copied, otherwise 0.
  */
static uint8_t app_threadx_thermal_ai_get_result_snapshot(thermal_ai_result_t *result_ptr)
{
  uint8_t result_valid = 0U;

  if ((result_ptr == NULL) ||
      (app_thermal_ai_is_enabled() == 0U) ||
      (g_thermal_ai_model_ready == 0U))
  {
    return 0U;
  }

  if (tx_mutex_get(&g_thermal_ai_mutex, TX_NO_WAIT) == TX_SUCCESS)
  {
    if ((g_thermal_ai_runtime_enable != 0U) && (g_thermal_ai_model_ready != 0U))
    {
      *result_ptr = g_thermal_ai_runtime.last_result;
      result_valid = 1U;
    }
    tx_mutex_put(&g_thermal_ai_mutex);
  }

  return result_valid;
}

/**
  * @brief  Composite abnormal-hotspot rectangles into one complete RGB565 frame.
  * @param  frame Destination RGB565 frame.
  * @param  frame_width Destination width in pixels.
  * @param  frame_height Destination height in pixels.
  * @param  result_ptr Latest hotspot result.
  * @retval None
  */
static void app_threadx_gui_draw_ai_boxes_rgb565(uint16_t *frame,
                                                  uint16_t frame_width,
                                                  uint16_t frame_height,
                                                  const thermal_ai_result_t *result_ptr)
{
  uint32_t detection_index;
  uint16_t border_thickness;

  if ((frame == NULL) || (frame_width == 0U) || (frame_height == 0U) || (result_ptr == NULL))
  {
    return;
  }

  border_thickness = (frame_width >= CFG_GUI_FULLSCREEN_WIDTH) ? 4U : 3U;
  for (detection_index = 0U; detection_index < result_ptr->detection_count; detection_index++)
  {
    const thermal_ai_detection_t *detection_ptr = &result_ptr->detections[detection_index];
    thermal_ai_bbox_t scaled_bbox;
    uint16_t x_min;
    uint16_t y_min;
    uint16_t x_max;
    uint16_t y_max;
    uint16_t thickness_index;
    uint16_t x;
    uint16_t y;
    const uint16_t border_color = 0xFFE0U;

    if ((detection_ptr->valid == 0U) ||
        (detection_ptr->class_id != (uint8_t)THERMAL_AI_CLASS_CIRCUIT_BOARD_ABNORMAL_HOTSPOT))
    {
      continue;
    }

    thermal_ai_runtime_scale_bbox(&detection_ptr->bbox,
                                  CFG_THERMAL_AI_INPUT_WIDTH,
                                  CFG_THERMAL_AI_INPUT_HEIGHT,
                                  frame_width,
                                  frame_height,
                                  &scaled_bbox);
    x_min = scaled_bbox.x_min;
    y_min = scaled_bbox.y_min;
    x_max = scaled_bbox.x_max;
    y_max = scaled_bbox.y_max;

    if ((x_max <= x_min) || (y_max <= y_min))
    {
      continue;
    }

    for (thickness_index = 0U; thickness_index < border_thickness; thickness_index++)
    {
      uint16_t top = (uint16_t)(y_min + thickness_index);
      uint16_t bottom = (y_max > thickness_index) ? (uint16_t)(y_max - thickness_index) : y_max;
      uint16_t left = (uint16_t)(x_min + thickness_index);
      uint16_t right = (x_max > thickness_index) ? (uint16_t)(x_max - thickness_index) : x_max;

      if ((top >= frame_height) || (bottom >= frame_height) ||
          (left >= frame_width) || (right >= frame_width) ||
          (right <= left) || (bottom <= top))
      {
        break;
      }

      for (x = left; x <= right; x++)
      {
        frame[(uint32_t)top * frame_width + x] = border_color;
        frame[(uint32_t)bottom * frame_width + x] = border_color;
      }
      for (y = top; y <= bottom; y++)
      {
        frame[(uint32_t)y * frame_width + left] = border_color;
        frame[(uint32_t)y * frame_width + right] = border_color;
      }
    }
  }
}

/**
  * @brief  Thermal rendering thread entry.
  * @param  thread_input Unused thread input parameter.
  * @retval None
  */
static VOID app_threadx_thermal_thread_entry(ULONG thread_input)
{
  uint32_t ai_frame_counter_last = 0U;
  ULONG ai_tick_last = 0U;

  TX_PARAMETER_NOT_USED(thread_input);

  while (tiny1c_thermal_app_start() != IR_SUCCESS)
  {
    g_tiny1c_app_started = 0U;
    tx_thread_sleep(TX_TIMER_TICKS_PER_SECOND);
  }

  {
    float probe_temp_c = 0.0f;

    (void)app_threadx_libirtemp_temp_correct(0.95f, 16384U, 25.0f, 30.0f, &probe_temp_c);
    g_libirtemp_probe_sink_c = probe_temp_c;
  }

  g_tiny1c_app_started = 1U;

  for (;;)
  {
    uint32_t frame_counter_now;
    const uint16_t *temp14_frame;

    tiny1c_thermal_app_process();
    frame_counter_now = tiny1c_thermal_app_get_frame_counter();
    if ((frame_counter_now != 0U) && (frame_counter_now != ai_frame_counter_last))
    {
      temp14_frame = tiny1c_thermal_app_get_temp14_frame();
      if ((CFG_THERMAL_AI_ENABLE != 0U) &&
          (app_thermal_ai_is_enabled() != 0U) &&
          (temp14_frame != NULL) &&
          ((HAL_GetTick() - ai_tick_last) >= CFG_THERMAL_AI_MIN_INTERVAL_MS))
      {
        app_threadx_thermal_ai_run_inference(temp14_frame, frame_counter_now);
        ai_frame_counter_last = frame_counter_now;
        ai_tick_last = HAL_GetTick();
      }
    }
    tx_thread_sleep(1U);
  }
}

/**
  * @brief  Publish calibrated frame extrema to the GUI cache at a fixed rate.
  * @param  thread_input Unused thread input parameter.
  * @retval None
  */
static VOID app_threadx_extrema_update_thread_entry(ULONG thread_input)
{
  TX_PARAMETER_NOT_USED(thread_input);

  for (;;)
  {
    if (g_tiny1c_app_started != 0U)
    {
      tiny1c_thermal_extrema_t extrema;

      if (tiny1c_thermal_app_get_frame_extrema(&extrema) != 0U)
      {
        g_extrema_cache_valid = 0U;
        __DMB();
        g_extrema_cache_max_temp14 = extrema.max_temp14;
        g_extrema_cache_min_temp14 = extrema.min_temp14;
        g_extrema_cache_max_temp_x = extrema.max_temp_x;
        g_extrema_cache_max_temp_y = extrema.max_temp_y;
        g_extrema_cache_min_temp_x = extrema.min_temp_x;
        g_extrema_cache_min_temp_y = extrema.min_temp_y;
        __DMB();
        g_extrema_cache_valid = 1U;

        if (extrema.center_temp14 != 0U)
        {
          tiny1c_thermal_app_set_center_temp_centi_c(
              app_threadx_temp14_to_compensated_centi_celsius(extrema.center_temp14));
        }
      }
    }

    tx_thread_sleep((CFG_EXTREMA_UPDATE_PERIOD_TICKS > 0U) ? CFG_EXTREMA_UPDATE_PERIOD_TICKS : 1U);
  }
}

/**
  * @brief  Low-priority battery polling thread entry.
  * @param  thread_input Unused thread input parameter.
  * @retval None
  */
static VOID app_threadx_battery_thread_entry(ULONG thread_input)
{
  uint8_t battery_initialized = 0U;

  TX_PARAMETER_NOT_USED(thread_input);

  (void)memset(&g_battery_device, 0, sizeof(g_battery_device));

  for (;;)
  {
    HAL_StatusTypeDef hal_status = HAL_ERROR;

    if (app_i2c4_bus_lock() == TX_SUCCESS)
    {
      if (battery_initialized == 0U)
      {
        hal_status = BQ27441_Init(&g_battery_device,
                                  &hi2c4,
                                  BQ27441_DEFAULT_ADDR,
                                  CFG_BATTERY_I2C_TIMEOUT_MS);
        if (hal_status == HAL_OK)
        {
          battery_initialized = 1U;
          hal_status = BQ27441_Update(&g_battery_device);
        }
      }
      else
      {
        hal_status = BQ27441_Update(&g_battery_device);
      }

      app_i2c4_bus_unlock();
    }

    if (hal_status == HAL_OK)
    {
      g_battery_cache_percent = g_battery_device.soc;
      g_battery_cache_charge_state = (uint8_t)g_battery_device.charge_status;
      g_battery_cache_valid = 1U;
    }
    else if (battery_initialized == 0U)
    {
      g_battery_cache_valid = 0U;
    }

    tx_thread_sleep((CFG_BATTERY_POLL_PERIOD_TICKS > 0U) ? CFG_BATTERY_POLL_PERIOD_TICKS : 1U);
  }
}

/**
  * @brief  Restore the CubeMX Pipe1 RAW10-to-RGB565 configuration after a CSI fault.
  * @retval int32_t APP_CAMERA_IMX219_OK or APP_CAMERA_IMX219_ERROR_DCMIPP.
  */
static int32_t app_threadx_imx219_reconfigure_dcmipp(void)
{
  DCMIPP_CSI_PIPE_ConfTypeDef csi_pipe_config =
  {
    .DataTypeMode = DCMIPP_DTMODE_DTIDA,
    .DataTypeIDA = DCMIPP_DT_RAW10,
    .DataTypeIDB = DCMIPP_DT_RAW10,
  };
  DCMIPP_CSI_ConfTypeDef csi_config =
  {
    .PHYBitrate = DCMIPP_CSI_PHY_BT_900,
    .DataLaneMapping = DCMIPP_CSI_PHYSICAL_DATA_LANES,
    .NumberOfLanes = DCMIPP_CSI_TWO_DATA_LANES,
  };
  DCMIPP_PipeConfTypeDef pipe_config =
  {
    .FrameRate = DCMIPP_FRAME_RATE_ALL,
    .PixelPipePitch = CFG_IMX219_FRAME_WIDTH * sizeof(uint16_t),
    .PixelPackerFormat = DCMIPP_PIXEL_PACKER_FORMAT_RGB565_1,
  };
  DCMIPP_RawBayer2RGBConfTypeDef raw_bayer_config =
  {
    .VLineStrength = DCMIPP_RAWBAYER_ALGO_NONE,
    .HLineStrength = DCMIPP_RAWBAYER_ALGO_NONE,
    .RawBayerType = DCMIPP_RAWBAYER_RGGB,
    .PeakStrength = DCMIPP_RAWBAYER_ALGO_NONE,
    .EdgeStrength = DCMIPP_RAWBAYER_ALGO_NONE,
  };
  int32_t status = APP_CAMERA_IMX219_ERROR_DCMIPP;

  HAL_NVIC_DisableIRQ(CSI_IRQn);
  __HAL_RCC_CSI_FORCE_RESET();
  __DSB();
  __HAL_RCC_CSI_RELEASE_RESET();
  HAL_NVIC_ClearPendingIRQ(CSI_IRQn);

  if ((HAL_DCMIPP_Init(&hdcmipp) != HAL_OK) ||
      (HAL_DCMIPP_CSI_PIPE_SetConfig(&hdcmipp, DCMIPP_PIPE1, &csi_pipe_config) != HAL_OK) ||
      (HAL_DCMIPP_CSI_SetConfig(&hdcmipp, &csi_config) != HAL_OK) ||
      (HAL_DCMIPP_PIPE_SetConfig(&hdcmipp, DCMIPP_PIPE1, &pipe_config) != HAL_OK) ||
      (HAL_DCMIPP_CSI_SetVCConfig(&hdcmipp,
                                  DCMIPP_VIRTUAL_CHANNEL0,
                                  DCMIPP_CSI_DT_BPP10) != HAL_OK) ||
      (HAL_DCMIPP_PIPE_SetISPRawBayer2RGBConfig(&hdcmipp,
                                                DCMIPP_PIPE1,
                                                &raw_bayer_config) != HAL_OK) ||
      (HAL_DCMIPP_PIPE_EnableISPRawBayer2RGB(&hdcmipp, DCMIPP_PIPE1) != HAL_OK))
  {
    status = APP_CAMERA_IMX219_ERROR_DCMIPP;
  }
  else
  {
    status = APP_CAMERA_IMX219_OK;
  }

  HAL_NVIC_ClearPendingIRQ(CSI_IRQn);
  HAL_NVIC_EnableIRQ(CSI_IRQn);
  return status;
}

/**
  * @brief  Stop the sensor and Pipe1 before a bounded restart attempt.
  * @param  sensor_initialized Non-zero after the IMX219 register path was initialized.
  * @retval None
  */
static void app_threadx_imx219_stop_capture(uint8_t sensor_initialized)
{
  if (sensor_initialized != 0U)
  {
    (void)AppCameraIMX219_SetStream(0U);
  }

  if ((hdcmipp.PipeState[DCMIPP_PIPE1] != HAL_DCMIPP_PIPE_STATE_READY) &&
      (hdcmipp.PipeState[DCMIPP_PIPE1] != HAL_DCMIPP_PIPE_STATE_RESET))
  {
    (void)HAL_DCMIPP_CSI_PIPE_Stop(&hdcmipp, DCMIPP_PIPE1, DCMIPP_VIRTUAL_CHANNEL0);
  }

  g_imx219_completed_frame_address = 0U;
  g_imx219_last_frame_timestamp_ms = 0U;
  __DMB();
}

/**
  * @brief  Notify the low-priority IMX219 control loop that Pipe1 completed a frame.
  * @param  None
  * @retval None
  */
void app_imx219_on_frame_event(void)
{
  uint32_t completed_address = HAL_DCMIPP_PIPE_GetMemoryAddress(&hdcmipp,
                                                                DCMIPP_PIPE1,
                                                                DCMIPP_MEMORY_ADDRESS_0);

  if ((completed_address == (uint32_t)g_imx219_rgb565_frame_a) ||
      (completed_address == (uint32_t)g_imx219_rgb565_frame_b))
  {
    g_imx219_completed_frame_address = (uintptr_t)completed_address;
    g_imx219_last_frame_timestamp_ms = HAL_GetTick();
    __DMB();
    g_imx219_runtime_frame_counter++;
  }
  else
  {
    g_imx219_last_error_detail = completed_address;
    g_imx219_error_count++;
    g_imx219_last_dcmipp_error = completed_address;
    g_imx219_dcmipp_error_count++;
    g_imx219_recovery_reason = APP_CAMERA_IMX219_ERROR_DCMIPP;
    g_imx219_recovery_requested = 1U;
  }
}

/**
  * @brief  Record a DCMIPP fault and request recovery from thread context.
  * @param  error_code HAL DCMIPP error code.
  * @retval None
  */
void app_imx219_on_dcmipp_error(uint32_t error_code)
{
  g_imx219_last_error_detail = error_code;
  g_imx219_error_count++;
  g_imx219_last_dcmipp_error = error_code;
  g_imx219_dcmipp_error_count++;
  g_imx219_recovery_reason = APP_CAMERA_IMX219_ERROR_DCMIPP;
  g_imx219_recovery_requested = 1U;
}

/**
  * @brief  Record a CSI line error and defer recovery to thread context.
  * @param  data_lane CSI data-lane index reported by HAL.
  * @param  error_code HAL CSI error bitmap.
  * @retval None
  */
void app_imx219_on_csi_line_error(uint32_t data_lane, uint32_t error_code)
{
  g_imx219_last_error_detail = ((data_lane & 0xFFU) << 24) |
                                (error_code & 0x00FFFFFFU);
  g_imx219_error_count++;
  g_imx219_last_csi_error_lane = data_lane;
  g_imx219_last_csi_error_code = error_code;
  g_imx219_csi_line_error_count++;
  g_imx219_recovery_reason = APP_CAMERA_IMX219_ERROR_CSI;
  g_imx219_recovery_requested = 1U;
}

/**
  * @brief  Bring up IMX219 and run low-rate AE/AWB without blocking Tiny1C controls.
  * @param  thread_input Unused thread input parameter.
  * @retval None
  */
static VOID app_threadx_imx219_thread_entry(ULONG thread_input)
{
  const AppCameraIMX219Config_t camera_config =
  {
    .width = CFG_IMX219_FRAME_WIDTH,
    .height = CFG_IMX219_FRAME_HEIGHT,
    .fps = 15U,
    .input_clock_hz = 24000000UL,
  };
  uint32_t frame_counter_last = 0U;
  uint32_t stream_start_tick_ms = 0U;
  uint32_t frame_watchdog_tick_ms;
  uint16_t chip_id = 0U;
  uint8_t sensor_initialized = 0U;

  TX_PARAMETER_NOT_USED(thread_input);
  tx_thread_sleep((CFG_IMX219_STARTUP_DELAY_MS * TX_TIMER_TICKS_PER_SECOND + 999U) / 1000U);

  for (;;)
  {
    AppCameraISPStats_t stats;
    int32_t status;

    if (g_imx219_runtime_status != APP_CAMERA_IMX219_OK)
    {
      app_threadx_imx219_stop_capture(sensor_initialized);
      sensor_initialized = 0U;
      g_imx219_recovery_requested = 0U;
      status = AppCameraIMX219_Init(&camera_config, &chip_id);
      g_imx219_runtime_chip_id = chip_id;
      if (status == APP_CAMERA_IMX219_OK)
      {
        sensor_initialized = 1U;
        status = app_threadx_imx219_reconfigure_dcmipp();
      }
      if (status == APP_CAMERA_IMX219_OK)
      {
        status = AppCameraISP_ConfigureIspChain(&hdcmipp, DCMIPP_PIPE1);
      }
      g_imx219_completed_frame_address = 0U;
      g_imx219_last_frame_timestamp_ms = 0U;
      if ((status == APP_CAMERA_IMX219_OK) &&
          (HAL_DCMIPP_CSI_PIPE_DoubleBufferStart(&hdcmipp,
                                                  DCMIPP_PIPE1,
                                                  DCMIPP_VIRTUAL_CHANNEL0,
                                                  (uint32_t)g_imx219_rgb565_frame_a,
                                                  (uint32_t)g_imx219_rgb565_frame_b,
                                                  DCMIPP_MODE_CONTINUOUS) != HAL_OK))
      {
        status = APP_CAMERA_IMX219_ERROR_DCMIPP;
      }
      if (status == APP_CAMERA_IMX219_OK)
      {
        status = AppCameraIMX219_SetStream(1U);
      }

      g_imx219_runtime_status = status;
      if (status != APP_CAMERA_IMX219_OK)
      {
        app_threadx_imx219_stop_capture(sensor_initialized);
        sensor_initialized = 0U;
        tx_thread_sleep((CFG_IMX219_RETRY_DELAY_MS * TX_TIMER_TICKS_PER_SECOND + 999U) / 1000U);
        continue;
      }
      stream_start_tick_ms = HAL_GetTick();
      frame_counter_last = g_imx219_runtime_frame_counter;
    }

    if (g_imx219_recovery_requested != 0U)
    {
      g_imx219_runtime_status = g_imx219_recovery_reason;
      app_threadx_imx219_stop_capture(sensor_initialized);
      sensor_initialized = 0U;
      g_imx219_recovery_count++;
      tx_thread_sleep((CFG_IMX219_RETRY_DELAY_MS * TX_TIMER_TICKS_PER_SECOND + 999U) / 1000U);
      continue;
    }

    frame_watchdog_tick_ms = (g_imx219_last_frame_timestamp_ms != 0U) ?
                             g_imx219_last_frame_timestamp_ms : stream_start_tick_ms;
    if ((HAL_GetTick() - frame_watchdog_tick_ms) > CFG_IMX219_NO_FRAME_TIMEOUT_MS)
    {
      g_imx219_error_count++;
      g_imx219_last_error_detail = 0U;
      g_imx219_no_frame_count++;
      g_imx219_runtime_status = APP_CAMERA_IMX219_ERROR_NO_FRAME;
      app_threadx_imx219_stop_capture(sensor_initialized);
      sensor_initialized = 0U;
      g_imx219_recovery_count++;
      tx_thread_sleep((CFG_IMX219_RETRY_DELAY_MS * TX_TIMER_TICKS_PER_SECOND + 999U) / 1000U);
      continue;
    }

    if (g_imx219_runtime_frame_counter != frame_counter_last)
    {
      app_imx219_frame_snapshot_t frame_snapshot;

      frame_counter_last = g_imx219_runtime_frame_counter;
      if (app_threadx_imx219_get_frame_snapshot(&frame_snapshot) != 0U)
      {
        app_threadx_dcache_invalidate_by_addr((void *)frame_snapshot.frame, CFG_IMX219_FRAME_BYTES);
        AppCameraISP_SampleRgb565(frame_snapshot.frame,
                                  CFG_IMX219_FRAME_WIDTH,
                                  CFG_IMX219_FRAME_HEIGHT,
                                  CFG_IMX219_FRAME_WIDTH * sizeof(uint16_t),
                                  &stats);
        AppCameraISP_RunAutoExposure(stats.luma_avg);
        AppCameraISP_RunAutoWhiteBalance(&hdcmipp,
                                         DCMIPP_PIPE1,
                                         stats.r_avg8,
                                         stats.g_avg8,
                                         stats.b_avg8);
      }
    }

    tx_thread_sleep((CFG_IMX219_CONTROL_PERIOD_MS * TX_TIMER_TICKS_PER_SECOND + 999U) / 1000U);
  }
}

/**
  * @brief  GUI rendering thread entry.
  * @param  thread_input Unused thread input parameter.
  * @retval None
  */
static VOID app_threadx_gui_thread_entry(ULONG thread_input)
{
  uint32_t thermal_frame_counter_last = 0U;
  uint32_t visible_frame_counter_last = 0U;
  uint32_t render_generation_last = 0U;
  uint32_t overlay_update_tick_last = 0U;
  uint8_t fusion_visible_pending = 0U;

  TX_PARAMETER_NOT_USED(thread_input);

  rgblcd_init();
  lv_init();
  lv_port_disp_init();
  lv_port_indev_init();
  setup_ui(&guider_ui);
  events_init(&guider_ui);
  custom_init(&guider_ui);
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(NULL);

  g_gui_preview_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  g_gui_preview_img_dsc.header.always_zero = 0U;
  app_threadx_gui_render_camera_frame(g_gui_preview_rgb565_frame,
                                      CFG_GUI_PREVIEW_WIDTH,
                                      CFG_GUI_PREVIEW_HEIGHT);
  g_gui_preview_img_dsc.header.w = CFG_GUI_PREVIEW_WIDTH;
  g_gui_preview_img_dsc.header.h = CFG_GUI_PREVIEW_HEIGHT;
  g_gui_preview_img_dsc.data_size = CFG_GUI_PREVIEW_WIDTH * CFG_GUI_PREVIEW_HEIGHT * sizeof(uint16_t);
  g_gui_preview_img_dsc.data = (const uint8_t *)g_gui_preview_rgb565_frame;
  lv_img_set_src(guider_ui.WidgetsDemo_preview_img, &g_gui_preview_img_dsc);

  g_gui_fullscreen_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  g_gui_fullscreen_img_dsc.header.always_zero = 0U;
  g_gui_fullscreen_img_dsc.header.w = CFG_GUI_FULLSCREEN_WIDTH;
  g_gui_fullscreen_img_dsc.header.h = CFG_GUI_FULLSCREEN_HEIGHT;
  g_gui_fullscreen_img_dsc.data_size = CFG_GUI_FULLSCREEN_WIDTH * CFG_GUI_FULLSCREEN_HEIGHT * sizeof(uint16_t);
  g_gui_fullscreen_img_dsc.data = (const uint8_t *)g_gui_fullscreen_rgb565_frame;
  lv_img_set_src(guider_ui.WidgetsDemo_fullscreen_preview_img, &g_gui_fullscreen_img_dsc);

  for (;;)
  {
    uint32_t thermal_frame_counter_now;
    uint32_t visible_frame_counter_now;
    uint32_t render_generation_now;
    uint32_t now_ms;
    thermal_ai_result_t ai_draw_result;
    char time_text[16];
    char battery_text[16];
    char ai_text[24];
    uint32_t seconds;
    uint8_t abnormal_detected = 0U;

    app_camera_view_mode_t camera_mode = g_camera_view_mode;
    uint8_t render_needed = 0U;
    uint8_t visible_available;

    thermal_frame_counter_now = tiny1c_thermal_app_get_frame_counter();
    visible_frame_counter_now = g_imx219_runtime_frame_counter;
    visible_available = (g_imx219_completed_frame_address != 0U) ? 1U : 0U;
    render_generation_now = g_camera_render_generation;

    if ((visible_available != 0U) &&
        (visible_frame_counter_now != visible_frame_counter_last))
    {
      fusion_visible_pending = 1U;
    }
    else if (visible_available == 0U)
    {
      fusion_visible_pending = 0U;
    }

    if (render_generation_now != render_generation_last)
    {
      render_needed = 1U;
    }
    else if ((camera_mode == APP_CAMERA_VIEW_THERMAL) &&
             (thermal_frame_counter_now != 0U) &&
             (thermal_frame_counter_now != thermal_frame_counter_last))
    {
      render_needed = 1U;
    }
    else if ((camera_mode == APP_CAMERA_VIEW_VISIBLE) &&
             (visible_frame_counter_now != 0U) &&
             (visible_frame_counter_now != visible_frame_counter_last))
    {
      render_needed = 1U;
    }
    else if ((camera_mode == APP_CAMERA_VIEW_FUSION) &&
              (((visible_available != 0U) && (fusion_visible_pending != 0U) &&
                ((visible_frame_counter_now != visible_frame_counter_last) ||
                 (thermal_frame_counter_now != thermal_frame_counter_last))) ||
               ((visible_available == 0U) &&
                (thermal_frame_counter_now != thermal_frame_counter_last))))
    {
      render_needed = 1U;
    }

    thermal_frame_counter_last = thermal_frame_counter_now;
    visible_frame_counter_last = visible_frame_counter_now;
    render_generation_last = render_generation_now;

    if (render_needed != 0U)
    {
      uint8_t ai_draw_result_valid = app_threadx_thermal_ai_get_result_snapshot(&ai_draw_result);
      uint8_t render_ok;

      if (thermal_gui_is_fullscreen_active() != 0U)
      {
        render_ok = app_threadx_gui_render_camera_frame(g_gui_fullscreen_rgb565_frame,
                                                        CFG_GUI_FULLSCREEN_WIDTH,
                                                        CFG_GUI_FULLSCREEN_HEIGHT);
        if ((render_ok != 0U) &&
            (camera_mode == APP_CAMERA_VIEW_THERMAL) &&
            (ai_draw_result_valid != 0U))
        {
          app_threadx_gui_draw_ai_boxes_rgb565(g_gui_fullscreen_rgb565_frame,
                                                CFG_GUI_FULLSCREEN_WIDTH,
                                                CFG_GUI_FULLSCREEN_HEIGHT,
                                                &ai_draw_result);
        }
        if (render_ok != 0U)
        {
          lv_obj_invalidate(guider_ui.WidgetsDemo_fullscreen_preview_img);
        }
      }
      else
      {
        render_ok = app_threadx_gui_render_camera_frame(g_gui_preview_rgb565_frame,
                                                        CFG_GUI_PREVIEW_WIDTH,
                                                        CFG_GUI_PREVIEW_HEIGHT);
        if ((render_ok != 0U) &&
            (camera_mode == APP_CAMERA_VIEW_THERMAL) &&
            (ai_draw_result_valid != 0U))
        {
          app_threadx_gui_draw_ai_boxes_rgb565(g_gui_preview_rgb565_frame,
                                                CFG_GUI_PREVIEW_WIDTH,
                                                CFG_GUI_PREVIEW_HEIGHT,
                                                &ai_draw_result);
        }
        if (render_ok != 0U)
        {
          lv_obj_invalidate(guider_ui.WidgetsDemo_preview_img);
        }
      }

      if (render_ok == 0U)
      {
        g_camera_render_drop_count++;
      }
      else if ((camera_mode == APP_CAMERA_VIEW_FUSION) &&
               (visible_available != 0U))
      {
        fusion_visible_pending = 0U;
      }
    }

    now_ms = HAL_GetTick();
    if ((now_ms - overlay_update_tick_last) >= CFG_GUI_OVERLAY_UPDATE_PERIOD_MS)
    {
      abnormal_detected = app_threadx_gui_update_preview(&guider_ui);
      thermal_gui_auto_snapshot_process(abnormal_detected, now_ms);
      overlay_update_tick_last = now_ms;
    }

    seconds = now_ms / 1000U;
    (void)snprintf(time_text, sizeof(time_text), "%02lu:%02lu",
                   (unsigned long)((seconds / 60U) % 60U),
                   (unsigned long)(seconds % 60U));
    app_threadx_gui_set_badge_text(guider_ui.WidgetsDemo_status_time, time_text);
    app_threadx_gui_format_battery_text(battery_text, sizeof(battery_text));
    app_threadx_gui_set_badge_text(guider_ui.WidgetsDemo_status_power, battery_text);
    app_threadx_gui_format_ai_status_text(ai_text, sizeof(ai_text));
    app_threadx_gui_set_badge_text(guider_ui.WidgetsDemo_status_uart, ai_text);

    lv_timer_handler();
    tx_thread_sleep(5U);
  }
}

/**
  * @brief  Report whether any owner currently holds UART file mode.
  * @retval uint8_t 1 when file mode is active, otherwise 0.
  */
static uint8_t app_threadx_uart_file_mode_active_internal(void)
{
  return (g_uart_file_hold_mask != 0U) ? 1U : 0U;
}

/**
  * @brief  Set one UART file-mode hold bit.
  * @param  hold_mask Owner bit mask.
  * @retval None
  */
static void app_threadx_uart_file_hold_set(uint32_t hold_mask)
{
  app_uart_request_file_mode(hold_mask);
}

/**
  * @brief  Clear one UART file-mode hold bit.
  * @param  hold_mask Owner bit mask.
  * @retval None
  */
static void app_threadx_uart_file_hold_clear(uint32_t hold_mask)
{
  app_uart_release_file_mode(hold_mask);
}

/**
  * @brief  Wait until any in-flight UART DMA transmission completes.
  * @retval None
  */
static void app_threadx_uart_wait_for_tx_idle(void)
{
  while (g_uart_stream_tx_busy != 0U)
  {
    tx_thread_sleep(1U);
  }
}

/**
  * @brief  Send one text response over USART1 using blocking mode.
  * @param  text_ptr Null-terminated ASCII text.
  * @retval UINT HAL/FileX-style status code.
  */
static UINT app_threadx_uart_send_text(const char *text_ptr)
{
  HAL_StatusTypeDef hal_status;
  uint32_t text_length;

  if (text_ptr == NULL)
  {
    return FX_PTR_ERROR;
  }

  text_length = (uint32_t)strlen(text_ptr);
  if (text_length == 0U)
  {
    return FX_SUCCESS;
  }

  app_threadx_uart_wait_for_tx_idle();
  g_uart_stream_tx_busy = 1U;
  hal_status = HAL_UART_Transmit(&huart1, (uint8_t *)(uintptr_t)text_ptr, (uint16_t)text_length, 5000U);
  g_uart_stream_tx_busy = 0U;
  return (hal_status == HAL_OK) ? FX_SUCCESS : FX_IO_ERROR;
}

/**
  * @brief  Format and send one text response over USART1.
  * @param  format_ptr Printf-style format string.
  * @retval UINT HAL/FileX-style status code.
  */
static UINT app_threadx_uart_send_text_fmt(const char *format_ptr, ...)
{
  int text_length;
  va_list args;

  if (format_ptr == NULL)
  {
    return FX_PTR_ERROR;
  }

  va_start(args, format_ptr);
  text_length = vsnprintf((char *)g_uart_command_tx_line, sizeof(g_uart_command_tx_line), format_ptr, args);
  va_end(args);
  if ((text_length < 0) || ((uint32_t)text_length >= sizeof(g_uart_command_tx_line)))
  {
    return FX_BUFFER_ERROR;
  }

  return app_threadx_uart_send_text((const char *)g_uart_command_tx_line);
}

/**
  * @brief  Encode one binary chunk into Base64 text.
  * @param  input_ptr Source byte buffer.
  * @param  input_size Number of source bytes.
  * @param  output_ptr Output text buffer.
  * @param  output_capacity Size of @p output_ptr in bytes.
  * @retval uint32_t Number of output bytes written without terminator.
  */
static uint32_t app_threadx_base64_encode(const uint8_t *input_ptr,
                                          uint32_t input_size,
                                          char *output_ptr,
                                          uint32_t output_capacity)
{
  static const char g_base64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint32_t input_index = 0U;
  uint32_t output_index = 0U;

  if ((input_ptr == NULL) || (output_ptr == NULL))
  {
    return 0U;
  }

  while (input_index < input_size)
  {
    uint32_t remain = input_size - input_index;
    uint8_t b0 = input_ptr[input_index++];
    uint8_t b1 = (remain > 1U) ? input_ptr[input_index++] : 0U;
    uint8_t b2 = (remain > 2U) ? input_ptr[input_index++] : 0U;

    if ((output_index + 4U) >= output_capacity)
    {
      return 0U;
    }

    output_ptr[output_index++] = g_base64_table[(b0 >> 2) & 0x3FU];
    output_ptr[output_index++] = g_base64_table[((b0 & 0x03U) << 4) | ((b1 >> 4) & 0x0FU)];
    output_ptr[output_index++] = (remain > 1U) ? g_base64_table[((b1 & 0x0FU) << 2) | ((b2 >> 6) & 0x03U)] : '=';
    output_ptr[output_index++] = (remain > 2U) ? g_base64_table[b2 & 0x3FU] : '=';
  }

  output_ptr[output_index] = '\0';
  return output_index;
}

/**
  * @brief  Send the current snapshot file list over the serial file protocol.
  * @retval UINT FileX status code.
  */
static UINT app_threadx_uart_send_snapshot_list(void)
{
  UINT status;
  uint32_t entry_count = 0U;
  uint32_t entry_index;

  status = app_filex_snapshot_get_list((CHAR *)g_uart_file_snapshot_names,
                                       CFG_UART_FILE_MAX_LISTED_SNAPSHOTS,
                                       CFG_UART_FILE_NAME_STRIDE,
                                       &entry_count);
  if (status != FX_SUCCESS)
  {
    return status;
  }

  status = app_threadx_uart_send_text_fmt("LIST %lu\n", (unsigned long)entry_count);
  if (status != FX_SUCCESS)
  {
    return status;
  }

  for (entry_index = 0U; entry_index < entry_count; entry_index++)
  {
    status = app_threadx_uart_send_text_fmt("NAME %s\n", g_uart_file_snapshot_names[entry_index]);
    if (status != FX_SUCCESS)
    {
      return status;
    }
  }

  return app_threadx_uart_send_text("LIST_END\n");
}

/**
  * @brief  Send one named snapshot as raw RGB565 chunks over the serial file protocol.
  * @param  file_name_ptr Snapshot file name in THMxxxxx.BMP format.
  * @retval UINT FileX/HAL status code.
  */
static UINT app_threadx_uart_send_snapshot_named(const CHAR *file_name_ptr)
{
  UINT status;
  uint16_t width = 0U;
  uint16_t height = 0U;
  uint32_t total_bytes;
  const uint8_t *payload_ptr;
  uint32_t payload_offset = 0U;
  uint32_t sequence = 0U;

  if (file_name_ptr == NULL)
  {
    return FX_PTR_ERROR;
  }

  status = app_filex_snapshot_load_rgb565(file_name_ptr,
                                          g_uart_file_rgb565_frame,
                                          CFG_UART_FILE_MAX_PIXELS,
                                          &width,
                                          &height);
  if (status != FX_SUCCESS)
  {
    return status;
  }

  total_bytes = (uint32_t)width * (uint32_t)height * sizeof(uint16_t);
  payload_ptr = (const uint8_t *)(const void *)g_uart_file_rgb565_frame;

  status = app_threadx_uart_send_text_fmt("FILE_BEGIN %s %u %u %lu\n",
                                          file_name_ptr,
                                          (unsigned int)width,
                                          (unsigned int)height,
                                          (unsigned long)total_bytes);
  if (status != FX_SUCCESS)
  {
    return status;
  }

  while (payload_offset < total_bytes)
  {
    uint32_t chunk_bytes = total_bytes - payload_offset;
    uint32_t encoded_bytes;

    if (chunk_bytes > CFG_UART_FILE_BASE64_CHUNK_BYTES)
    {
      chunk_bytes = CFG_UART_FILE_BASE64_CHUNK_BYTES;
    }

    encoded_bytes = app_threadx_base64_encode(&payload_ptr[payload_offset],
                                              chunk_bytes,
                                              (char *)g_uart_file_chunk_base64,
                                              sizeof(g_uart_file_chunk_base64));
    if (encoded_bytes == 0U)
    {
      return FX_BUFFER_ERROR;
    }

    status = app_threadx_uart_send_text_fmt("DATA %lu %s\n",
                                            (unsigned long)sequence,
                                            g_uart_file_chunk_base64);
    if (status != FX_SUCCESS)
    {
      return status;
    }

    payload_offset += chunk_bytes;
    sequence++;
  }

  return app_threadx_uart_send_text("FILE_END\n");
}

/**
  * @brief  Send the latest available snapshot over the serial file protocol.
  * @retval UINT FileX/HAL status code.
  */
static UINT app_threadx_uart_send_snapshot_latest(void)
{
  UINT status;
  uint32_t entry_count = 0U;

  status = app_filex_snapshot_get_list((CHAR *)g_uart_file_snapshot_names,
                                       CFG_UART_FILE_MAX_LISTED_SNAPSHOTS,
                                       CFG_UART_FILE_NAME_STRIDE,
                                       &entry_count);
  if (status != FX_SUCCESS)
  {
    return status;
  }
  if (entry_count == 0U)
  {
    return FX_NO_MORE_ENTRIES;
  }

  return app_threadx_uart_send_snapshot_named(g_uart_file_snapshot_names[entry_count - 1U]);
}

/**
  * @brief  Process one ASCII file-protocol command line.
  * @param  command_line_ptr Null-terminated command text.
  * @retval None
  */
static void app_threadx_uart_process_command_line(const uint8_t *command_line_ptr)
{
  UINT status = FX_SUCCESS;
  const char *command_ptr = (const char *)command_line_ptr;
  const char *actual_mode_text = NULL;

  if ((command_line_ptr == NULL) || (command_line_ptr[0] == '\0'))
  {
    return;
  }

  if (strcmp(command_ptr, "FILE_ENTER") == 0)
  {
    app_threadx_uart_file_hold_set(APP_UART_FILE_HOLD_WEB);
    status = app_filex_prepare();
    if (status == FX_SUCCESS)
    {
      actual_mode_text = (app_threadx_uart_file_mode_active_internal() != 0U) ? "FILE_MODE" : "STREAM_MODE";
      (void)app_threadx_uart_send_text_fmt("OK %s\n", actual_mode_text);
    }
    else
    {
      app_threadx_uart_file_hold_clear(APP_UART_FILE_HOLD_WEB);
      (void)app_threadx_uart_send_text_fmt("ERR %s\n", app_filex_status_to_string(status));
    }
    return;
  }

  if (strcmp(command_ptr, "FILE_EXIT") == 0)
  {
    app_threadx_uart_file_hold_clear(APP_UART_FILE_HOLD_WEB);
    actual_mode_text = (app_threadx_uart_file_mode_active_internal() != 0U) ? "FILE_MODE" : "STREAM_MODE";
    (void)app_threadx_uart_send_text_fmt("OK %s\n", actual_mode_text);
    return;
  }

  if ((strcmp(command_ptr, "FILE_LIST") == 0) || (strcmp(command_ptr, "FILE_GET_LATEST") == 0) ||
      (strncmp(command_ptr, "FILE_GET ", 9) == 0))
  {
    app_threadx_uart_file_hold_set(APP_UART_FILE_HOLD_WEB);
    g_uart_web_file_hold_expire_tick = tx_time_get() + ((CFG_UART_FILE_WEB_TIMEOUT_TICKS > 0U) ? CFG_UART_FILE_WEB_TIMEOUT_TICKS : 1U);
  }

  if (strcmp(command_ptr, "FILE_LIST") == 0)
  {
    status = app_threadx_uart_send_snapshot_list();
  }
  else if (strcmp(command_ptr, "FILE_GET_LATEST") == 0)
  {
    status = app_threadx_uart_send_snapshot_latest();
  }
  else if (strncmp(command_ptr, "FILE_GET ", 9) == 0)
  {
    status = app_threadx_uart_send_snapshot_named((const CHAR *)(command_ptr + 9));
  }
  else
  {
    status = FX_INVALID_PATH;
  }

  if (status != FX_SUCCESS)
  {
    (void)app_threadx_uart_send_text_fmt("ERR %s\n", app_filex_status_to_string(status));
  }
}

/**
  * @brief  Pop one byte captured by the USART1 receive interrupt.
  * @param  rx_byte_ptr Destination byte pointer.
  * @retval uint8_t 1 when a byte was returned, otherwise 0.
  */
static uint8_t app_threadx_uart_command_rx_pop(uint8_t *rx_byte_ptr)
{
  uint16_t tail;

  if (rx_byte_ptr == NULL)
  {
    return 0U;
  }

  tail = g_uart_command_rx_tail;
  if (tail == g_uart_command_rx_head)
  {
    return 0U;
  }

  __DMB();
  *rx_byte_ptr = g_uart_command_rx_ring[tail];
  g_uart_command_rx_tail = (uint16_t)((tail + 1U) & CFG_UART_COMMAND_RX_RING_MASK);
  return 1U;
}

/**
  * @brief  UART command thread entry for gallery/file transfer commands.
  * @param  thread_input Unused thread input parameter.
  * @retval None
  */
static VOID app_threadx_uart_command_thread_entry(ULONG thread_input)
{
  uint32_t command_length = 0U;

  TX_PARAMETER_NOT_USED(thread_input);
  g_uart_command_rx_head = 0U;
  g_uart_command_rx_tail = 0U;
  (void)HAL_UART_Receive_IT(&huart1, &g_uart_command_rx_irq_byte, 1U);

  for (;;)
  {
    uint8_t rx_byte = 0U;
    ULONG tick_now;

    if ((g_uart_file_hold_mask & APP_UART_FILE_HOLD_WEB) != 0U)
    {
      tick_now = tx_time_get();
      if (((int32_t)(tick_now - g_uart_web_file_hold_expire_tick) >= 0) && (g_uart_web_file_hold_expire_tick != 0U))
      {
        app_threadx_uart_file_hold_clear(APP_UART_FILE_HOLD_WEB);
      }
    }

    if (app_threadx_uart_command_rx_pop(&rx_byte) == 0U)
    {
      if (huart1.RxState == HAL_UART_STATE_READY)
      {
        (void)HAL_UART_Receive_IT(&huart1, &g_uart_command_rx_irq_byte, 1U);
      }
      tx_thread_sleep(1U);
      continue;
    }

    if (rx_byte == '\r')
    {
      continue;
    }

    if (rx_byte == '\n')
    {
      g_uart_command_rx_line[command_length] = '\0';
      if (command_length > 0U)
      {
        app_threadx_uart_process_command_line(g_uart_command_rx_line);
      }
      command_length = 0U;
      continue;
    }

    if (command_length < (sizeof(g_uart_command_rx_line) - 1U))
    {
      g_uart_command_rx_line[command_length++] = rx_byte;
    }
    else
    {
      command_length = 0U;
      (void)app_threadx_uart_send_text("ERR Buffer\n");
    }
  }
}

/**
  * @brief  UART streaming thread entry.
  * @param  thread_input Unused thread input parameter.
  * @retval None
  */
static VOID app_threadx_uart_stream_thread_entry(ULONG thread_input)
{
  uint32_t frame_counter_last = 0U;

  TX_PARAMETER_NOT_USED(thread_input);

  for (;;)
  {
    uint32_t frame_counter_now;
    const uint16_t *temp14_frame;
    uint32_t tx_bytes;

    if (g_tiny1c_app_started == 0U)
    {
      tx_thread_sleep(10U);
      continue;
    }

    if (app_threadx_uart_file_mode_active_internal() != 0U)
    {
      tx_thread_sleep(10U);
      continue;
    }

    if (g_uart_stream_tx_busy != 0U)
    {
      tx_thread_sleep(1U);
      continue;
    }

    frame_counter_now = tiny1c_thermal_app_get_frame_counter();
    if ((frame_counter_now == 0U) || (frame_counter_now == frame_counter_last))
    {
      tx_thread_sleep(CFG_UART_STREAM_PERIOD_TICKS);
      continue;
    }

    temp14_frame = tiny1c_thermal_app_get_temp14_frame();
    if (temp14_frame == NULL)
    {
      tx_thread_sleep(CFG_UART_STREAM_PERIOD_TICKS);
      continue;
    }

    tx_bytes = app_threadx_uart_stream_build_packet(g_uart_stream_tx_buffer, temp14_frame, frame_counter_now);
    if (tx_bytes == 0U)
    {
      tx_thread_sleep(CFG_UART_STREAM_PERIOD_TICKS);
      continue;
    }

    app_threadx_uart_stream_clean_dcache(g_uart_stream_tx_buffer, tx_bytes);
    if (HAL_UART_Transmit_DMA(&huart1, g_uart_stream_tx_buffer, tx_bytes) == HAL_OK)
    {
      g_uart_stream_tx_busy = 1U;
      frame_counter_last = frame_counter_now;
    }

    tx_thread_sleep(CFG_UART_STREAM_PERIOD_TICKS);
  }
}

/**
  * @brief  Build one downsampled temperature packet for UART transmission.
  * @param  tx_buffer Pointer to the transmission buffer.
  * @param  temp14_frame Pointer to the latest 14-bit temperature frame.
  * @param  frame_counter Processed frame sequence number.
  * @retval uint32_t Total packet length in bytes.
  */
static uint32_t app_threadx_uart_stream_build_packet(uint8_t *tx_buffer, const uint16_t *temp14_frame, uint32_t frame_counter)
{
  app_threadx_uart_stream_header_t *header_ptr;
  uint16_t *payload_ptr;
  uint32_t y_out;
  uint32_t x_out;
  uint16_t min_temp14 = 0xFFFFU;
  uint16_t max_temp14 = 0U;
  uint16_t center_temp14;

  if ((tx_buffer == NULL) || (temp14_frame == NULL))
  {
    return 0U;
  }

  header_ptr = (app_threadx_uart_stream_header_t *)tx_buffer;
  payload_ptr = (uint16_t *)(void *)(tx_buffer + sizeof(app_threadx_uart_stream_header_t));
  center_temp14 = 0U;

  for (y_out = 0U; y_out < CFG_UART_STREAM_FRAME_HEIGHT; y_out++)
  {
    uint32_t y_base = y_out * CFG_UART_STREAM_DOWNSAMPLE_STEP_Y;

    for (x_out = 0U; x_out < CFG_UART_STREAM_FRAME_WIDTH; x_out++)
    {
      uint32_t x_base = x_out * CFG_UART_STREAM_DOWNSAMPLE_STEP_X;
      uint32_t y_inner;
      uint32_t x_inner;
      uint32_t sum_temp14 = 0U;
      uint16_t temp14_value;

      for (y_inner = 0U; y_inner < CFG_UART_STREAM_DOWNSAMPLE_STEP_Y; y_inner++)
      {
        for (x_inner = 0U; x_inner < CFG_UART_STREAM_DOWNSAMPLE_STEP_X; x_inner++)
        {
          sum_temp14 += app_threadx_temp14_get_preview_oriented_sample(
              temp14_frame,
              CFG_UART_STREAM_SOURCE_WIDTH,
              CFG_UART_STREAM_SOURCE_HEIGHT,
              (uint16_t)(x_base + x_inner),
              (uint16_t)(y_base + y_inner));
        }
      }

      temp14_value = (uint16_t)(sum_temp14 / (CFG_UART_STREAM_DOWNSAMPLE_STEP_X * CFG_UART_STREAM_DOWNSAMPLE_STEP_Y));
      payload_ptr[(y_out * CFG_UART_STREAM_FRAME_WIDTH) + x_out] = temp14_value;

      if (temp14_value < min_temp14)
      {
        min_temp14 = temp14_value;
      }

      if (temp14_value > max_temp14)
      {
        max_temp14 = temp14_value;
      }
    }
  }

  center_temp14 = payload_ptr[((CFG_UART_STREAM_FRAME_HEIGHT / 2U) * CFG_UART_STREAM_FRAME_WIDTH) +
                              (CFG_UART_STREAM_FRAME_WIDTH / 2U)];

  header_ptr->sync_word = CFG_UART_STREAM_SYNC_WORD;
  header_ptr->packet_type = CFG_UART_STREAM_PACKET_TYPE_TEMP14;
  header_ptr->frame_counter = frame_counter;
  header_ptr->frame_width = CFG_UART_STREAM_FRAME_WIDTH;
  header_ptr->frame_height = CFG_UART_STREAM_FRAME_HEIGHT;
  header_ptr->payload_bytes = (uint16_t)CFG_UART_STREAM_PAYLOAD_BYTES;
  header_ptr->center_temp14 = center_temp14;
  header_ptr->min_temp14 = min_temp14;
  header_ptr->max_temp14 = max_temp14;

  return (uint32_t)(sizeof(app_threadx_uart_stream_header_t) + CFG_UART_STREAM_PAYLOAD_BYTES);
}

/**
  * @brief  Clean D-Cache for a DMA transmission buffer.
  * @param  buffer_addr Buffer start address.
  * @param  buffer_size Buffer length in bytes.
  * @retval None
  */
static void app_threadx_uart_stream_clean_dcache(void *buffer_addr, uint32_t buffer_size)
{
  app_threadx_dcache_clean_by_addr(buffer_addr, buffer_size);
}

/**
  * @brief  UART DMA transmission complete callback.
  * @param  uart_handle Pointer to the UART handle.
  * @retval None
  */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart_handle)
{
  if ((uart_handle != NULL) && (uart_handle->Instance == USART1))
  {
    g_uart_stream_tx_busy = 0U;
  }
}

/**
  * @brief  USART1 receive-complete callback for the file-command ring buffer.
  * @param  uart_handle Pointer to the UART handle.
  * @retval None
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart_handle)
{
  if ((uart_handle != NULL) && (uart_handle->Instance == USART1))
  {
    uint16_t head = g_uart_command_rx_head;
    uint16_t next_head = (uint16_t)((head + 1U) & CFG_UART_COMMAND_RX_RING_MASK);

    if (next_head != g_uart_command_rx_tail)
    {
      g_uart_command_rx_ring[head] = g_uart_command_rx_irq_byte;
      __DMB();
      g_uart_command_rx_head = next_head;
    }
    (void)HAL_UART_Receive_IT(&huart1, &g_uart_command_rx_irq_byte, 1U);
  }
}

/**
  * @brief  UART error callback.
  * @param  uart_handle Pointer to the UART handle.
  * @retval None
  */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart_handle)
{
  if ((uart_handle != NULL) && (uart_handle->Instance == USART1))
  {
    g_uart_stream_tx_busy = 0U;
    if (huart1.RxState == HAL_UART_STATE_READY)
    {
      (void)HAL_UART_Receive_IT(&huart1, &g_uart_command_rx_irq_byte, 1U);
    }
  }
}

/* USER CODE END 1 */
