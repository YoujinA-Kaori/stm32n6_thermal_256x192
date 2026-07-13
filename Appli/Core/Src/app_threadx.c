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
#include <math.h>
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
#include "thermal.h"
#ifndef APP_HAS_PARALLEL_NETWORKS
#define APP_HAS_PARALLEL_NETWORKS 0
#endif
#include "../../../Middlewares/ST/AI/Npu/ll_aton/ll_aton_runtime.h"
#include "../../../Middlewares/ST/AI/Npu/ll_aton/ll_aton_rt_user_api.h"
#include "../../../Middlewares/ST/AI/Npu/ll_aton/ll_aton_osal.h"
#include "../../../Middlewares/ST/AI/Npu/ll_aton/ll_aton_caches_interface.h"
#include "thermal_ai_runtime.h"
#include "thermal_project_config.h"
#include "BQ27441/bq27441g1a.h"
#include "Tiny1C/tiny1c_thermal_app.h"
#include "Tiny1C/tiny1c_vdcmd_app.h"
#include "usart.h"
#include "RGBLCD/rgblcd.h"

/* USER CODE END Includes */

extern volatile uint8_t g_ll_aton_rt_irq_err_latched;
extern volatile uint32_t g_ll_aton_rt_irq_err_irqs_low32;
extern volatile uint32_t g_ll_aton_rt_irq_err_busif_or;
extern volatile uint32_t g_ll_aton_rt_irq_err_epoch_irq;
void LL_ATON_RT_ClearIrqErrorState(void);

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
  uint8_t class_id;
  uint16_t confidence_permille;
  float score;
  float x_min;
  float y_min;
  float x_max;
  float y_max;
} app_threadx_ai_candidate_t;

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
#define CFG_THERMAL_AI_MIN_INTERVAL_TICKS     (((CFG_THERMAL_AI_MIN_INTERVAL_MS * TX_TIMER_TICKS_PER_SECOND) + 999U) / 1000U)
#define CFG_THERMAL_AI_DIAG_STAGE_SETTLE_MS   150U
#define CFG_THERMAL_AI_DIAG_STAGE_SETTLE_TICKS (((CFG_THERMAL_AI_DIAG_STAGE_SETTLE_MS * TX_TIMER_TICKS_PER_SECOND) + 999U) / 1000U)

/* Keep the existing 160x120 CubeAI network dormant until it is migrated.
 * The active path detects hotspots from the calibrated temp14 frame. */
#define CFG_THERMAL_AI_ENABLE                 1U
#define CFG_THERMAL_AI_PREVIEW_HOTSPOT_ONLY   1U
#define CFG_THERMAL_AI_TEMP14_HOTSPOT_ONLY    1U
#define CFG_THERMAL_AI_RGB565_BOX_COMPOSITE   1U
#define CFG_THERMAL_AI_USE_REFERENCE_INPUT    0U
#define CFG_THERMAL_AI_SELF_TEST_ENABLE       0U
#define CFG_UART_COMMAND_THREAD_STACK_SIZE    4096U
#define CFG_UART_COMMAND_THREAD_PRIORITY      16U
#define CFG_UART_FILE_WEB_TIMEOUT_MS          4000U
#define CFG_UART_COMMAND_LINE_MAX             96U
#define CFG_UART_COMMAND_RX_RING_SIZE          128U
#define CFG_UART_COMMAND_RX_RING_MASK          (CFG_UART_COMMAND_RX_RING_SIZE - 1U)
#define CFG_UART_COMMAND_TX_LINE_MAX          2304U
#define CFG_UART_FILE_BASE64_CHUNK_BYTES      1536U
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
#define CFG_THERMAL_AI_INPUT_WIDTH            CFG_THERMAL_SENSOR_WIDTH
#define CFG_THERMAL_AI_INPUT_HEIGHT           CFG_THERMAL_SENSOR_HEIGHT
#define CFG_THERMAL_AI_INPUT_PIXELS           (CFG_THERMAL_AI_INPUT_WIDTH * CFG_THERMAL_AI_INPUT_HEIGHT)
#define CFG_THERMAL_AI_GRID_WIDTH             32U
#define CFG_THERMAL_AI_GRID_HEIGHT            24U
#define CFG_THERMAL_AI_OUTPUT_CHANNELS        8U
#define CFG_THERMAL_AI_DETECTION_CLASS_COUNT  1U
#define CFG_THERMAL_AI_OUTPUT_BYTES           (CFG_THERMAL_AI_GRID_WIDTH * CFG_THERMAL_AI_GRID_HEIGHT * CFG_THERMAL_AI_OUTPUT_CHANNELS)
#define CFG_THERMAL_AI_ALLOW_PLANAR_OUTPUT_FALLBACK 0U
#define CFG_THERMAL_AI_WEIGHT_BACKING_XSPI2   1U
#define CFG_THERMAL_AI_WEIGHT_SIG0            0xF6U
#define CFG_THERMAL_AI_WEIGHT_SIG1            0xA3U
#define CFG_THERMAL_AI_WEIGHT_SIG2            0x1DU
#define CFG_THERMAL_AI_WEIGHT_SIG3            0x23U
#define CFG_THERMAL_AI_WEIGHT_BLOB_ADDR       ((uintptr_t)0x71000000UL)
#define CFG_THERMAL_AI_WEIGHT_BLOB_BYTES      61736U
#define CFG_THERMAL_AI_WEIGHT_BLOB_CACHE_BYTES 61760U
#define CFG_THERMAL_AI_WEIGHT_BLOB_FNV1A32    0x6DFF01E8UL
#define CFG_THERMAL_AI_HISTOGRAM_BINS         16384U
#define CFG_THERMAL_AI_MAX_CANDIDATES         (CFG_THERMAL_AI_GRID_WIDTH * CFG_THERMAL_AI_GRID_HEIGHT)
#define CFG_THERMAL_AI_BACKGROUND_PERCENT_NUM 50U
#define CFG_THERMAL_AI_BACKGROUND_PERCENT_DEN 100U
#define CFG_THERMAL_AI_DELTA_TEMP14_MIN       (-128.0f)
#define CFG_THERMAL_AI_DELTA_TEMP14_MAX       (960.0f)
#define CFG_THERMAL_AI_DELTA_TEMP14_SPAN      (CFG_THERMAL_AI_DELTA_TEMP14_MAX - CFG_THERMAL_AI_DELTA_TEMP14_MIN)
#define CFG_THERMAL_AI_VALID_TEMP14_MIN       3730U
#define CFG_THERMAL_AI_VALID_TEMP14_MAX       7250U
#define CFG_THERMAL_AI_INVALID_PIXEL_MAX_PERMILLE 50U
#define CFG_THERMAL_AI_OBJECTNESS_THRESHOLD   0.76f
#define CFG_THERMAL_AI_CLASS_THRESHOLD        0.72f
#define CFG_THERMAL_AI_SCORE_THRESHOLD        0.996f
#define CFG_THERMAL_AI_ABNORMAL_SCORE_THRESHOLD 0.996f
#define CFG_THERMAL_AI_ENABLE_GEOMETRY_POSTFILTER 1U
#define CFG_THERMAL_AI_NMS_IOU_THRESHOLD      0.15f
#define CFG_THERMAL_AI_MAX_DETECTIONS         2U
#define CFG_THERMAL_AI_NORMAL_MIN_WIDTH_PX    96.0f
#define CFG_THERMAL_AI_NORMAL_MIN_HEIGHT_PX   64.0f
#define CFG_THERMAL_AI_NORMAL_MIN_AREA_PX     28160.0f
#define CFG_THERMAL_AI_ABNORMAL_MIN_WIDTH_PX  10.0f
#define CFG_THERMAL_AI_ABNORMAL_MIN_HEIGHT_PX 10.0f
#define CFG_THERMAL_AI_ABNORMAL_MIN_AREA_PX   1080.0f
#define CFG_THERMAL_AI_ABNORMAL_MAX_AREA_PX   6400.0f
#define CFG_THERMAL_AI_MIN_DISPLAY_BOX_PX     8U
#define CFG_THERMAL_AI_DRAW_NORMAL_BOXES      0U
#define CFG_THERMAL_AI_HOTSPOT_FALLBACK_ENABLE 1U
#define CFG_THERMAL_AI_HOTSPOT_FALLBACK_DELTA_TEMP14 120U
#define CFG_THERMAL_AI_TOPRIGHT_HOTSPOT_DELTA_TEMP14 160U
#define CFG_THERMAL_AI_HOTSPOT_FALLBACK_MIN_PIXELS   100U
#define CFG_THERMAL_AI_HOTSPOT_FALLBACK_BOX_MIN_PX   38U
#define CFG_THERMAL_AI_HOTSPOT_FALLBACK_BOX_MAX_PX   80U
#define CFG_THERMAL_AI_HOTSPOT_FALLBACK_CONFIDENCE_MIN_PERMILLE 550U
#define CFG_THERMAL_AI_HOTSPOT_FALLBACK_CONFIDENCE_MAX_PERMILLE 920U
#define CFG_THERMAL_AI_PREVIEW_SCORE_BINS            256U
#define CFG_THERMAL_AI_PREVIEW_HOT_SCORE_DELTA       112U
#define CFG_THERMAL_AI_PREVIEW_MIN_HOT_SCORE         224U
#define CFG_THERMAL_AI_PREVIEW_STRICT_DELTA_ADD       64U
#define CFG_THERMAL_AI_PREVIEW_STRICT_MIN_SCORE_ADD   31U
#define CFG_THERMAL_AI_PREVIEW_STRICT_MIN_PIXELS      300U
#define CFG_THERMAL_AI_PREVIEW_STRICT_CONFIRM_DELTA_TEMP14 160U
#define CFG_THERMAL_AI_PREVIEW_CONFIRM_VALID_MAX_TEMP14 16383U
#define CFG_THERMAL_AI_PREVIEW_CONFIRM_DELTA_TEMP14  48U
#define CFG_THERMAL_AI_TOPRIGHT_BIAS_COMP_ENABLE     1U
#define CFG_THERMAL_AI_TOPRIGHT_BIAS_SCORE           80U
#define CFG_THERMAL_AI_COLD_PIT_CORRECTION_ENABLE 1U
#define CFG_THERMAL_AI_COLD_PIT_DELTA_TEMP14      32U
#define CFG_THERMAL_AI_COLD_PIT_NEIGHBOR_SPAN_MAX 48U
#define CFG_THERMAL_AI_COLD_PIT_MIN_NEIGHBORS     6U
#define CFG_THERMAL_AI_COLD_PIT_HOTTER_MARGIN     4U

#if defined(LL_ATON_THERMAL_USER_ALLOCATED_INPUTS) && (LL_ATON_THERMAL_USER_ALLOCATED_INPUTS > 0)
#define CFG_THERMAL_AI_GENERATED_USER_INPUTS 1U
#else
#define CFG_THERMAL_AI_GENERATED_USER_INPUTS 0U
#endif

#if defined(LL_ATON_THERMAL_USER_ALLOCATED_OUTPUTS) && (LL_ATON_THERMAL_USER_ALLOCATED_OUTPUTS > 0)
#define CFG_THERMAL_AI_GENERATED_USER_OUTPUTS 1U
#else
#define CFG_THERMAL_AI_GENERATED_USER_OUTPUTS 0U
#endif

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
static TX_MUTEX g_thermal_ai_mutex;
static ULONG g_thermal_thread_stack[CFG_THERMAL_THREAD_STACK_SIZE / sizeof(ULONG)];
static ULONG g_gui_thread_stack[CFG_GUI_THREAD_STACK_SIZE / sizeof(ULONG)];
static ULONG g_uart_stream_thread_stack[CFG_UART_STREAM_THREAD_STACK_SIZE / sizeof(ULONG)];
static ULONG g_uart_command_thread_stack[CFG_UART_COMMAND_THREAD_STACK_SIZE / sizeof(ULONG)];
static ULONG g_extrema_update_thread_stack[CFG_EXTREMA_UPDATE_THREAD_STACK_SIZE / sizeof(ULONG)];
static ULONG g_battery_thread_stack[CFG_BATTERY_THREAD_STACK_SIZE / sizeof(ULONG)];
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
static uint16_t g_uart_file_rgb565_frame[CFG_UART_FILE_MAX_PIXELS]
  __attribute__((section(".EXTRAM"), aligned(32)));
static uint16_t g_thermal_ai_corrected_temp14[CFG_THERMAL_AI_INPUT_PIXELS]
  __attribute__((section(".EXTRAM"), aligned(32)));
static uint16_t g_thermal_ai_oriented_temp14[CFG_THERMAL_AI_INPUT_PIXELS];
static uint16_t g_thermal_ai_histogram[CFG_THERMAL_AI_HISTOGRAM_BINS];
static app_threadx_ai_candidate_t g_thermal_ai_candidates[CFG_THERMAL_AI_MAX_CANDIDATES];
static app_threadx_ai_candidate_t g_thermal_ai_kept_candidates[CFG_THERMAL_AI_MAX_CANDIDATES];
static thermal_ai_runtime_t g_thermal_ai_runtime;
static volatile uint8_t g_thermal_ai_runtime_enable = 0U;
static volatile uint8_t g_thermal_ai_preview_pseudo_mode = (uint8_t)PSEUDO_COLOR_MODE_5;
static uint8_t g_thermal_ai_overlay_ready = 0U;
static uint8_t g_thermal_ai_model_ready = 0U;
static uint32_t g_thermal_ai_last_inference_ms = 0U;
static volatile uint8_t g_thermal_ai_iac_fault_latched = 0U;
static volatile uint32_t g_thermal_ai_iac_fault_count = 0U;
static volatile uint32_t g_thermal_ai_iac_last_periph_id = 0U;
static volatile uint8_t g_thermal_ai_diag_stage = 0U;
static volatile uint32_t g_thermal_ai_diag_run_count = 0U;
static volatile uint16_t g_thermal_ai_diag_raw_candidate_count = 0U;
static volatile uint16_t g_thermal_ai_diag_postfilter_candidate_count = 0U;
static volatile uint16_t g_thermal_ai_diag_max_score_permille = 0U;
static volatile uint16_t g_thermal_ai_diag_max_objectness_permille = 0U;
static volatile uint8_t g_thermal_ai_diag_output_layout = 0U;
static volatile uint8_t g_thermal_ai_diag_epoch_index = 0U;
static volatile uint8_t g_thermal_ai_diag_io_reason = 0U;
static volatile uint8_t g_thermal_ai_diag_weights_ok = 0U;
static volatile uint8_t g_thermal_ai_diag_output_byte0 = 0U;
static volatile uint8_t g_thermal_ai_diag_aton_irq_error_latched = 0U;
static volatile uint16_t g_thermal_ai_diag_aton_irq_error_irqs = 0U;
static volatile uint16_t g_thermal_ai_diag_aton_irq_error_aux = 0U;
static volatile uint8_t g_thermal_ai_epoch_callback_last_index = 0U;
static volatile uint8_t g_thermal_ai_self_test_input_xor = 0U;
static volatile uint8_t g_thermal_ai_self_test_output_byte0 = 0U;
static volatile uint8_t g_thermal_ai_self_test_output_byte1 = 0U;
static volatile uint8_t g_thermal_ai_self_test_output_byte2 = 0U;
static volatile uint8_t g_thermal_ai_self_test_output_byte3 = 0U;
static volatile uint8_t g_thermal_ai_self_test_output_min = 0U;
static volatile uint8_t g_thermal_ai_self_test_output_max = 0U;
static volatile uint8_t g_thermal_ai_self_test_output_layout = 0U;
static volatile uint8_t g_thermal_ai_self_test_mid_valid = 0U;
static volatile uint8_t g_thermal_ai_self_test_mid_byte0 = 0U;
static volatile uint8_t g_thermal_ai_self_test_mid_byte1 = 0U;
static volatile uint8_t g_thermal_ai_self_test_mid_byte2 = 0U;
static volatile uint8_t g_thermal_ai_self_test_mid_byte3 = 0U;
static volatile uint8_t g_thermal_ai_self_test_pipeline_valid_mask = 0U;
static volatile uint8_t g_thermal_ai_self_test_pipeline_byte0_conv3 = 0U;
static volatile uint8_t g_thermal_ai_self_test_pipeline_byte0_conv9 = 0U;
static volatile uint8_t g_thermal_ai_self_test_pipeline_byte0_conv15 = 0U;
static volatile uint8_t g_thermal_ai_self_test_pipeline_byte0_conv21 = 0U;
static volatile uint8_t g_thermal_ai_self_test_epoch_index = 0U;
static volatile uint8_t g_thermal_ai_self_test_done = 0U;
static volatile uint8_t g_thermal_ai_self_test_pass = 0U;
static volatile uint8_t g_thermal_ai_self_test_aton_irq_error_latched = 0U;
static volatile uint16_t g_thermal_ai_self_test_aton_irq_error_irqs = 0U;
static volatile uint16_t g_thermal_ai_self_test_aton_irq_error_aux = 0U;
static uint8_t g_thermal_ai_ll_runtime_ready = 0U;
static uint8_t g_thermal_ai_ll_network_needs_reset = 0U;
static const LL_Buffer_InfoTypeDef *g_thermal_ai_input_info = NULL;
static const LL_Buffer_InfoTypeDef *g_thermal_ai_output_info = NULL;
#if (CFG_THERMAL_AI_WEIGHT_BACKING_XSPI2 == 0U)
static uint8_t g_thermal_ai_weight_blob_loaded = 0U;
#endif
static uint8_t g_thermal_ai_user_io_bound = 0U;
#if (CFG_THERMAL_AI_GENERATED_USER_INPUTS != 0U)
static int8_t g_thermal_ai_user_input_storage[CFG_THERMAL_AI_INPUT_PIXELS]
    __attribute__((aligned(32)));
#endif
#if (CFG_THERMAL_AI_GENERATED_USER_OUTPUTS != 0U)
static int8_t g_thermal_ai_user_output_storage[CFG_THERMAL_AI_OUTPUT_BYTES]
    __attribute__((aligned(32)));
#endif
static int8_t *g_thermal_ai_input_buffer = NULL;
static int8_t *g_thermal_ai_output_buffer = NULL;
static lv_obj_t *g_thermal_ai_preview_boxes[CFG_THERMAL_AI_RUNTIME_MAX_DETECTIONS];
static lv_obj_t *g_thermal_ai_preview_labels[CFG_THERMAL_AI_RUNTIME_MAX_DETECTIONS];
static lv_obj_t *g_thermal_ai_fullscreen_boxes[CFG_THERMAL_AI_RUNTIME_MAX_DETECTIONS];
static lv_obj_t *g_thermal_ai_fullscreen_labels[CFG_THERMAL_AI_RUNTIME_MAX_DETECTIONS];

#if ((CFG_THERMAL_AI_USE_REFERENCE_INPUT != 0U) || (CFG_THERMAL_AI_SELF_TEST_ENABLE != 0U))
extern const int8_t g_thermal_ai_reference_input[];
extern const int8_t g_thermal_ai_reference_input_end[];

__asm__(
    ".section .rodata\n"
    ".global g_thermal_ai_reference_input\n"
    "g_thermal_ai_reference_input:\n"
    ".incbin \"../../../thermal_ai/artifacts/reports/cubeai_inputs/best_model_int8_cubeai_padded_circuit_board_abnormal_hotspot_frame_00014002/input_model_dtype_nhwc.int8.raw\"\n"
    ".global g_thermal_ai_reference_input_end\n"
    "g_thermal_ai_reference_input_end:\n"
    ".balign 4\n");
#endif

#if (CFG_THERMAL_AI_WEIGHT_BACKING_XSPI2 == 0U)
extern const uint8_t g_thermal_ai_weight_blob[];
extern const uint8_t g_thermal_ai_weight_blob_end[];

__asm__(
    ".section .rodata\n"
    ".global g_thermal_ai_weight_blob\n"
    "g_thermal_ai_weight_blob:\n"
    ".incbin \"../../../thermal_ai/artifacts/cubeai/thermal_atonbuf.AXISRAM2.bin\"\n"
    ".global g_thermal_ai_weight_blob_end\n"
    "g_thermal_ai_weight_blob_end:\n"
    ".balign 4\n");
#endif

LL_ATON_DECLARE_NAMED_NN_INSTANCE_AND_INTERFACE(thermal);

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static VOID app_threadx_thermal_thread_entry(ULONG thread_input);
static VOID app_threadx_gui_thread_entry(ULONG thread_input);
static VOID app_threadx_uart_stream_thread_entry(ULONG thread_input);
static VOID app_threadx_uart_command_thread_entry(ULONG thread_input);
static VOID app_threadx_extrema_update_thread_entry(ULONG thread_input);
static VOID app_threadx_battery_thread_entry(ULONG thread_input);
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
static uint16_t __attribute__((unused)) app_threadx_rgb565_average4(uint16_t c00,
                                            uint16_t c10,
                                            uint16_t c01,
                                            uint16_t c11);
static uint8_t app_threadx_thermal_ai_query_preview_pseudo_mode(void);
static uint8_t app_threadx_thermal_ai_preview_hotness_score(uint16_t rgb565_pixel,
                                                            uint8_t pseudo_mode);
static uint8_t app_threadx_thermal_ai_preview_hot_score_delta_for_mode(uint8_t pseudo_mode);
static uint8_t app_threadx_thermal_ai_preview_min_hot_score_for_mode(uint8_t pseudo_mode);
static uint32_t app_threadx_thermal_ai_preview_min_hot_pixels_for_mode(uint8_t pseudo_mode);
static uint16_t app_threadx_thermal_ai_preview_confirm_delta_for_mode(uint8_t pseudo_mode);
static uint8_t app_threadx_thermal_ai_percentile_from_u8_histogram(const uint16_t *histogram_ptr,
                                                                   uint32_t pixel_count,
                                                                   uint32_t rank_num,
                                                                   uint32_t rank_den);
static uint8_t app_threadx_thermal_ai_is_topright_bias_region(uint16_t x,
                                                              uint16_t y);
static void app_threadx_gui_set_badge_text(lv_obj_t *badge, const char *text);
static uint8_t app_threadx_gui_update_preview(lv_ui *ui);
static void app_threadx_gui_update_fullscreen_image(void);
static void app_threadx_gui_format_battery_text(char *buffer, uint32_t buffer_size);
static void app_threadx_gui_format_ai_status_text(char *buffer, uint32_t buffer_size);
static void app_threadx_thermal_ai_set_diag_stage(uint8_t stage, uint8_t allow_settle);
static void app_threadx_thermal_ai_epoch_callback(LL_ATON_RT_Callbacktype_t ctype,
                                                  const NN_Instance_TypeDef *nn_instance,
                                                  const EpochBlock_ItemTypeDef *epoch_block);
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
static uint8_t app_threadx_thermal_ai_prepare_io(void);
static uint8_t app_threadx_thermal_ai_run_initialized_network(void);
static void app_threadx_thermal_ai_run_inference(const uint16_t *temp14_frame, uint32_t frame_counter);
static void app_threadx_thermal_ai_run_preview_hotspot(const uint16_t *temp14_frame, uint32_t frame_counter);
static uint16_t app_threadx_thermal_ai_prepare_preview_temp14(const uint16_t *temp14_frame);
static uint8_t app_threadx_thermal_ai_load_reference_input(uint16_t *background_temp14_ptr,
                                                           uint16_t *max_temp14_ptr);
static uint8_t app_threadx_thermal_ai_prepare_embedded_weights(void);
static uint8_t app_threadx_thermal_ai_prepare_weight_cache(void);
static uint8_t app_threadx_thermal_ai_prepare_runtime(void);
static void app_threadx_thermal_ai_release_runtime(void);
static uint8_t app_threadx_thermal_ai_calc_xor8(const int8_t *buffer, uint32_t length);
static const LL_Buffer_InfoTypeDef *app_threadx_thermal_ai_find_buffer_by_name(
    const LL_Buffer_InfoTypeDef *buffer_list_ptr,
    const char *buffer_name_ptr);
static uint8_t app_threadx_thermal_ai_capture_buffer_byte0(const LL_Buffer_InfoTypeDef *buffer_info_ptr,
                                                           uint8_t *byte0_ptr);
static uint8_t app_threadx_thermal_ai_capture_buffer_bytes(const LL_Buffer_InfoTypeDef *buffer_info_ptr,
                                                           uint8_t *byte0_ptr,
                                                           uint8_t *byte1_ptr,
                                                           uint8_t *byte2_ptr,
                                                           uint8_t *byte3_ptr);
static void app_threadx_thermal_ai_capture_output_signature(const int8_t *buffer,
                                                            uint32_t length,
                                                            uint8_t *byte0_ptr,
                                                            uint8_t *min_u8_ptr,
                                                            uint8_t *max_u8_ptr);
static uint8_t app_threadx_thermal_ai_probe_weight_signature(void);
static uint16_t app_threadx_thermal_ai_percentile_from_histogram(uint32_t pixel_count);
static void app_threadx_thermal_ai_build_corrected_temp14_frame(const uint16_t *source_frame,
                                                                uint16_t *dest_frame);
static uint8_t app_threadx_thermal_ai_build_input(const uint16_t *temp14_frame,
                                                  uint16_t *background_temp14_ptr,
                                                  uint16_t *max_temp14_ptr);
static int8_t app_threadx_thermal_ai_read_output_q7(uint32_t grid_y,
                                                    uint32_t grid_x,
                                                    uint32_t channel_index,
                                                    uint8_t channel_planar_layout);
static uint8_t app_threadx_thermal_ai_output_is_planar(void);
static uint32_t app_threadx_thermal_ai_collect_candidates(float output_scale,
                                                          int16_t output_zero_point,
                                                          uint8_t channel_planar_layout,
                                                          uint32_t *raw_candidate_count_ptr,
                                                          uint16_t *max_score_permille_ptr,
                                                          uint16_t *max_objectness_permille_ptr);
static float app_threadx_thermal_ai_score_threshold_for_class(uint32_t class_index);
static uint8_t app_threadx_thermal_ai_box_passes_postfilter(uint8_t class_id,
                                                            float x_min,
                                                            float y_min,
                                                            float x_max,
                                                            float y_max);
static void app_threadx_thermal_ai_decode_output(thermal_ai_result_t *result_ptr);
#if (CFG_THERMAL_AI_HOTSPOT_FALLBACK_ENABLE != 0U)
static uint8_t app_threadx_thermal_ai_result_has_hotspot(const thermal_ai_result_t *result_ptr);
static uint8_t app_threadx_thermal_ai_collect_preview_hotspot_box(uint16_t background_temp14,
                                                                  uint32_t *hot_pixel_count_ptr,
                                                                  uint16_t *max_x_ptr,
                                                                  uint16_t *max_y_ptr,
                                                                  uint16_t *x_min_ptr,
                                                                  uint16_t *y_min_ptr,
                                                                  uint16_t *x_max_ptr,
                                                                  uint16_t *y_max_ptr,
                                                                  uint8_t *background_score_ptr,
                                                                  uint8_t *peak_score_ptr);
static void app_threadx_thermal_ai_append_hotspot_fallback(thermal_ai_result_t *result_ptr,
                                                           uint16_t background_temp14);
#endif
static float app_threadx_thermal_ai_sigmoid(float value);
static float app_threadx_thermal_ai_candidate_iou(const app_threadx_ai_candidate_t *lhs_ptr,
                                                  const app_threadx_ai_candidate_t *rhs_ptr);
static uint8_t app_threadx_thermal_ai_class_index_to_runtime_id(uint32_t class_index);
static lv_color_t app_threadx_thermal_ai_color_for_class(uint8_t class_id);
static uint16_t app_threadx_thermal_ai_calc_hotspot_fallback_confidence(uint8_t preview_guided,
                                                                        uint32_t hot_pixel_count,
                                                                        uint32_t box_width,
                                                                        uint32_t box_height,
                                                                        uint8_t background_score,
                                                                        uint8_t peak_score);
static void app_threadx_dcache_clean_by_addr(void *buffer_addr, uint32_t buffer_size);
static void app_threadx_dcache_invalidate_by_addr(void *buffer_addr, uint32_t buffer_size);
static void app_threadx_gui_init_ai_overlays(void);
static void app_threadx_gui_init_ai_overlay_set(lv_obj_t *parent_obj,
                                                lv_obj_t **box_array,
                                                lv_obj_t **label_array);
static void app_threadx_gui_update_ai_overlay_set(lv_obj_t **box_array,
                                                  lv_obj_t **label_array,
                                                  int32_t origin_x,
                                                  int32_t origin_y,
                                                  uint16_t preview_width,
                                                  uint16_t preview_height,
                                                  const thermal_ai_result_t *result_ptr,
                                                  uint8_t hide_unconfirmed_abnormal);
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
  * @brief  Report whether UART file mode is currently active.
  * @retval uint8_t 1 when file mode is active, otherwise 0.
  */
uint8_t app_uart_is_file_mode_active(void)
{
  return app_threadx_uart_file_mode_active_internal();
}

/**
  * @brief  Enable or disable runtime thermal AI detection.
  * @param  enable Non-zero to run inference and draw boxes; zero to hide AI boxes.
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
      g_thermal_ai_diag_stage = 0U;
      g_thermal_ai_diag_raw_candidate_count = 0U;
      g_thermal_ai_diag_postfilter_candidate_count = 0U;
      g_thermal_ai_diag_max_score_permille = 0U;
      g_thermal_ai_diag_max_objectness_permille = 0U;
      g_thermal_ai_diag_epoch_index = 0U;
      g_thermal_ai_diag_io_reason = 0U;
      g_thermal_ai_diag_output_byte0 = 0U;
      g_thermal_ai_diag_aton_irq_error_latched = 0U;
      g_thermal_ai_diag_aton_irq_error_irqs = 0U;
      g_thermal_ai_diag_aton_irq_error_aux = 0U;
      g_thermal_ai_self_test_input_xor = 0U;
      g_thermal_ai_self_test_output_byte0 = 0U;
      g_thermal_ai_self_test_output_byte1 = 0U;
      g_thermal_ai_self_test_output_byte2 = 0U;
      g_thermal_ai_self_test_output_byte3 = 0U;
      g_thermal_ai_self_test_output_min = 0U;
      g_thermal_ai_self_test_output_max = 0U;
      g_thermal_ai_self_test_output_layout = 0U;
      g_thermal_ai_self_test_mid_valid = 0U;
      g_thermal_ai_self_test_mid_byte0 = 0U;
      g_thermal_ai_self_test_mid_byte1 = 0U;
      g_thermal_ai_self_test_mid_byte2 = 0U;
      g_thermal_ai_self_test_mid_byte3 = 0U;
      g_thermal_ai_self_test_pipeline_valid_mask = 0U;
      g_thermal_ai_self_test_pipeline_byte0_conv3 = 0U;
      g_thermal_ai_self_test_pipeline_byte0_conv9 = 0U;
      g_thermal_ai_self_test_pipeline_byte0_conv15 = 0U;
      g_thermal_ai_self_test_pipeline_byte0_conv21 = 0U;
      g_thermal_ai_self_test_epoch_index = 0U;
      g_thermal_ai_self_test_done = 0U;
      g_thermal_ai_self_test_pass = 0U;
      g_thermal_ai_self_test_aton_irq_error_latched = 0U;
      g_thermal_ai_self_test_aton_irq_error_irqs = 0U;
      g_thermal_ai_self_test_aton_irq_error_aux = 0U;
      LL_ATON_RT_ClearIrqErrorState();
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
  * @brief  Cache the current preview pseudo-color mode for preview-first hotspot detection.
  * @param  pseudo_mode Active Tiny1C pseudo-color mode.
  * @retval None
  */
void app_thermal_ai_set_preview_pseudo_mode(uint8_t pseudo_mode)
{
  if ((pseudo_mode < (uint8_t)PSEUDO_COLOR_MODE_1) ||
      (pseudo_mode > (uint8_t)PSEUDO_COLOR_MODE_11))
  {
    pseudo_mode = (uint8_t)PSEUDO_COLOR_MODE_5;
  }

  g_thermal_ai_preview_pseudo_mode = pseudo_mode;
}

/**
  * @brief  Return the cached preview pseudo-color mode.
  * @retval uint8_t Cached pseudo-color mode.
  */
uint8_t app_thermal_ai_get_preview_pseudo_mode(void)
{
  return app_threadx_thermal_ai_query_preview_pseudo_mode();
}

/**
  * @brief  Collect the currently visible AI overlay boxes for snapshot rendering.
  * @param  fullscreen Non-zero selects the full-screen overlay set; zero selects the normal preview set.
  * @param  box_array Output array receiving visible boxes.
  * @param  max_boxes Maximum number of entries available in @p box_array.
  * @retval uint32_t Number of valid boxes written.
  */
uint32_t app_thermal_ai_snapshot_collect_boxes(uint8_t fullscreen,
                                               app_thermal_ai_snapshot_box_t *box_array,
                                               uint32_t max_boxes)
{
  lv_obj_t **source_boxes;
  uint32_t written_count = 0U;
  uint32_t box_index;

  if ((box_array == NULL) || (max_boxes == 0U))
  {
    return 0U;
  }

  (void)memset(box_array, 0, sizeof(app_thermal_ai_snapshot_box_t) * max_boxes);
  source_boxes = (fullscreen != 0U) ? g_thermal_ai_fullscreen_boxes : g_thermal_ai_preview_boxes;

  for (box_index = 0U; box_index < CFG_THERMAL_AI_RUNTIME_MAX_DETECTIONS; box_index++)
  {
    lv_obj_t *box_obj = source_boxes[box_index];
    lv_obj_t *label_obj;
    lv_color32_t border_color32;
    const char *label_text;

    if ((box_obj == NULL) || (lv_obj_has_flag(box_obj, LV_OBJ_FLAG_HIDDEN) != false))
    {
      continue;
    }
    if (written_count >= max_boxes)
    {
      break;
    }

    label_obj = lv_obj_get_child(box_obj, 0);
    label_text = ((label_obj != NULL) && (lv_obj_check_type(label_obj, &lv_label_class) != false))
                   ? lv_label_get_text(label_obj)
                   : "";
    border_color32.full = lv_color_to32(lv_obj_get_style_border_color(box_obj, LV_PART_MAIN | LV_STATE_DEFAULT));

    box_array[written_count].valid = 1U;
    box_array[written_count].x = (uint16_t)lv_obj_get_x(box_obj);
    box_array[written_count].y = (uint16_t)lv_obj_get_y(box_obj);
    box_array[written_count].width = (uint16_t)lv_obj_get_width(box_obj);
    box_array[written_count].height = (uint16_t)lv_obj_get_height(box_obj);
    box_array[written_count].border_color_rgb888 = ((uint32_t)border_color32.ch.red << 16) |
                                                   ((uint32_t)border_color32.ch.green << 8) |
                                                   (uint32_t)border_color32.ch.blue;
    (void)snprintf(box_array[written_count].label_text,
                   sizeof(box_array[written_count].label_text),
                   "%s",
                   label_text);
    written_count++;
  }

  return written_count;
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
  * @brief  Average four RGB565 pixels using a light 2x2 box filter.
  * @param  c00 Top-left RGB565 pixel.
  * @param  c10 Top-right RGB565 pixel.
  * @param  c01 Bottom-left RGB565 pixel.
  * @param  c11 Bottom-right RGB565 pixel.
  * @retval Averaged RGB565 pixel.
  */
static uint16_t app_threadx_rgb565_average4(uint16_t c00,
                                            uint16_t c10,
                                            uint16_t c01,
                                            uint16_t c11)
{
  uint32_t red;
  uint32_t green;
  uint32_t blue;

  red = ((uint32_t)((c00 >> 11) & 0x1FU) +
         (uint32_t)((c10 >> 11) & 0x1FU) +
         (uint32_t)((c01 >> 11) & 0x1FU) +
         (uint32_t)((c11 >> 11) & 0x1FU) + 2U) >> 2;

  green = ((uint32_t)((c00 >> 5) & 0x3FU) +
           (uint32_t)((c10 >> 5) & 0x3FU) +
           (uint32_t)((c01 >> 5) & 0x3FU) +
           (uint32_t)((c11 >> 5) & 0x3FU) + 2U) >> 2;

  blue = ((uint32_t)(c00 & 0x1FU) +
          (uint32_t)(c10 & 0x1FU) +
          (uint32_t)(c01 & 0x1FU) +
          (uint32_t)(c11 & 0x1FU) + 2U) >> 2;

  return (uint16_t)(((red & 0x1FU) << 11) |
                    ((green & 0x3FU) << 5) |
                    (blue & 0x1FU));
}

/**
  * @brief  Read the current Tiny1C pseudo-color mode with a safe default.
  * @retval uint8_t Active pseudo-color mode.
  */
static uint8_t app_threadx_thermal_ai_query_preview_pseudo_mode(void)
{
  uint8_t pseudo_mode = g_thermal_ai_preview_pseudo_mode;

  if ((pseudo_mode == (uint8_t)PSEUDO_COLOR_MODE_2) ||
      (pseudo_mode < (uint8_t)PSEUDO_COLOR_MODE_1) ||
      (pseudo_mode > (uint8_t)PSEUDO_COLOR_MODE_11))
  {
    pseudo_mode = (uint8_t)PSEUDO_COLOR_MODE_5;
  }

  return pseudo_mode;
}

/**
  * @brief  Convert one RGB565 preview pixel into a pseudo-mode-aware hotness score.
  * @param  rgb565_pixel Preview pixel in RGB565.
  * @param  pseudo_mode Active pseudo-color mode.
  * @retval uint8_t Hotness score in range 0..255.
  */
static uint8_t app_threadx_thermal_ai_preview_hotness_score(uint16_t rgb565_pixel,
                                                            uint8_t pseudo_mode)
{
  uint32_t red = ((rgb565_pixel >> 11) & 0x1FU) * 255U / 31U;
  uint32_t green = ((rgb565_pixel >> 5) & 0x3FU) * 255U / 63U;
  uint32_t blue = (rgb565_pixel & 0x1FU) * 255U / 31U;
  uint32_t luma = (77U * red + 150U * green + 29U * blue) >> 8;
  uint32_t rgb_max = red;
  uint32_t rgb_min = red;
  uint32_t saturation;
  uint32_t hot_score;

  if (green > rgb_max) rgb_max = green;
  if (blue > rgb_max) rgb_max = blue;
  if (green < rgb_min) rgb_min = green;
  if (blue < rgb_min) rgb_min = blue;

  saturation = rgb_max - rgb_min;
  switch (pseudo_mode)
  {
    case PSEUDO_COLOR_MODE_1:
      return (uint8_t)luma;

    case PSEUDO_COLOR_MODE_11:
      return (uint8_t)(255U - luma);

    case PSEUDO_COLOR_MODE_3:
    case PSEUDO_COLOR_MODE_4:
    case PSEUDO_COLOR_MODE_5:
    case PSEUDO_COLOR_MODE_6:
    case PSEUDO_COLOR_MODE_7:
    case PSEUDO_COLOR_MODE_8:
    case PSEUDO_COLOR_MODE_9:
    case PSEUDO_COLOR_MODE_10:
    default:
      /* Module pseudo-color palettes place their hot end at red, yellow or
       * white. Preserve white through luma, promote red explicitly, and use
       * saturation only as a small separation from green/cyan mid-tones. */
      hot_score = (red > luma) ? red : luma;
      hot_score += saturation >> 2;
      return (uint8_t)((hot_score > 255U) ? 255U : hot_score);
  }
}

/**
  * @brief  Return the preview-score delta threshold for one pseudo-color mode.
  * @param  pseudo_mode Active pseudo-color mode.
  * @retval uint8_t Hot-score delta threshold.
  */
static uint8_t app_threadx_thermal_ai_preview_hot_score_delta_for_mode(uint8_t pseudo_mode)
{
  switch (pseudo_mode)
  {
    case PSEUDO_COLOR_MODE_4:  /* 铁红 */
    case PSEUDO_COLOR_MODE_5:  /* 锐化 */
    case PSEUDO_COLOR_MODE_9:  /* 医疗 */
    case PSEUDO_COLOR_MODE_10: /* 测试 */
      return (uint8_t)(CFG_THERMAL_AI_PREVIEW_HOT_SCORE_DELTA +
                       CFG_THERMAL_AI_PREVIEW_STRICT_DELTA_ADD);

    case PSEUDO_COLOR_MODE_6:
    case PSEUDO_COLOR_MODE_7:
    case PSEUDO_COLOR_MODE_8:
      return (CFG_THERMAL_AI_PREVIEW_HOT_SCORE_DELTA > 32U)
               ? (uint8_t)(CFG_THERMAL_AI_PREVIEW_HOT_SCORE_DELTA - 32U)
               : CFG_THERMAL_AI_PREVIEW_HOT_SCORE_DELTA;

    default:
      return CFG_THERMAL_AI_PREVIEW_HOT_SCORE_DELTA;
  }
}

/**
  * @brief  Return the minimum absolute preview hot-score threshold for one pseudo-color mode.
  * @param  pseudo_mode Active pseudo-color mode.
  * @retval uint8_t Minimum preview hot-score threshold.
  */
static uint8_t app_threadx_thermal_ai_preview_min_hot_score_for_mode(uint8_t pseudo_mode)
{
  switch (pseudo_mode)
  {
    case PSEUDO_COLOR_MODE_4:  /* 铁红 */
    case PSEUDO_COLOR_MODE_5:  /* 锐化 */
    case PSEUDO_COLOR_MODE_9:  /* 医疗 */
    case PSEUDO_COLOR_MODE_10: /* 测试 */
      return (uint8_t)(CFG_THERMAL_AI_PREVIEW_MIN_HOT_SCORE +
                       CFG_THERMAL_AI_PREVIEW_STRICT_MIN_SCORE_ADD);

    case PSEUDO_COLOR_MODE_6:
    case PSEUDO_COLOR_MODE_7:
    case PSEUDO_COLOR_MODE_8:
      return (CFG_THERMAL_AI_PREVIEW_MIN_HOT_SCORE > 32U)
               ? (uint8_t)(CFG_THERMAL_AI_PREVIEW_MIN_HOT_SCORE - 32U)
               : CFG_THERMAL_AI_PREVIEW_MIN_HOT_SCORE;

    default:
      return CFG_THERMAL_AI_PREVIEW_MIN_HOT_SCORE;
  }
}

/**
  * @brief  Return the minimum RGB565 hot-pixel count for one pseudo-color mode.
  * @param  pseudo_mode Active pseudo-color mode.
  * @retval uint32_t Minimum number of hot pixels required for one box.
  */
static uint32_t app_threadx_thermal_ai_preview_min_hot_pixels_for_mode(uint8_t pseudo_mode)
{
  switch (pseudo_mode)
  {
    case PSEUDO_COLOR_MODE_4:  /* 铁红 */
    case PSEUDO_COLOR_MODE_5:  /* 锐化 */
    case PSEUDO_COLOR_MODE_9:  /* 医疗 */
    case PSEUDO_COLOR_MODE_10: /* 测试 */
      return CFG_THERMAL_AI_PREVIEW_STRICT_MIN_PIXELS;

    default:
      return CFG_THERMAL_AI_HOTSPOT_FALLBACK_MIN_PIXELS;
  }
}

/**
  * @brief  Return the required temperature rise for RGB565 hotspot confirmation.
  * @param  pseudo_mode Active pseudo-color mode.
  * @retval uint16_t Required rise above frame background in kelvin*16 counts.
  */
static uint16_t app_threadx_thermal_ai_preview_confirm_delta_for_mode(uint8_t pseudo_mode)
{
  switch (pseudo_mode)
  {
    case PSEUDO_COLOR_MODE_4:  /* 铁红 */
    case PSEUDO_COLOR_MODE_5:  /* 锐化 */
    case PSEUDO_COLOR_MODE_9:  /* 医疗 */
    case PSEUDO_COLOR_MODE_10: /* 测试 */
      return CFG_THERMAL_AI_PREVIEW_STRICT_CONFIRM_DELTA_TEMP14;

    default:
      return 0U;
  }
}

/**
  * @brief  Return one percentile value from a small 8-bit histogram.
  * @param  histogram_ptr Histogram buffer with 256 bins.
  * @param  pixel_count Total count accumulated in the histogram.
  * @param  rank_num Percentile numerator.
  * @param  rank_den Percentile denominator.
  * @retval uint8_t Percentile score.
  */
static uint8_t app_threadx_thermal_ai_percentile_from_u8_histogram(const uint16_t *histogram_ptr,
                                                                   uint32_t pixel_count,
                                                                   uint32_t rank_num,
                                                                   uint32_t rank_den)
{
  uint32_t cumulative = 0U;
  uint32_t histogram_index;
  uint32_t rank;

  if ((histogram_ptr == NULL) || (pixel_count == 0U) || (rank_den == 0U))
  {
    return 0U;
  }

  rank = ((pixel_count - 1U) * rank_num) / rank_den;
  for (histogram_index = 0U; histogram_index < CFG_THERMAL_AI_PREVIEW_SCORE_BINS; histogram_index++)
  {
    cumulative += (uint32_t)histogram_ptr[histogram_index];
    if (cumulative > rank)
    {
      return (uint8_t)histogram_index;
    }
  }

  return 255U;
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
  * @brief  Format one short AI diagnostic string for the top status badge.
  * @param  buffer Output buffer pointer.
  * @param  buffer_size Output buffer size.
  * @retval None
  */
static void app_threadx_gui_format_ai_status_text(char *buffer, uint32_t buffer_size)
{
  const char *iac_name;
  thermal_ai_result_t ai_result_snapshot;
  uint32_t displayed_inference_ms = g_thermal_ai_last_inference_ms;
  uint8_t ai_result_valid = 0U;
  uint8_t aton_irq_error_latched = 0U;
  uint16_t max_score_permille = 0U;
  uint16_t max_objectness_permille = 0U;
  uint8_t self_test_done = 0U;
  uint8_t self_test_pass = 0U;
  uint8_t self_test_output_byte0 = 0U;
  uint8_t self_test_output_byte1 = 0U;
  uint8_t self_test_output_byte2 = 0U;
  uint8_t self_test_output_byte3 = 0U;
  uint8_t self_test_mid_valid = 0U;
  uint8_t self_test_mid_byte0 = 0U;
  uint8_t self_test_mid_byte1 = 0U;
  uint8_t self_test_mid_byte2 = 0U;
  uint8_t self_test_mid_byte3 = 0U;
  uint8_t self_test_pipeline_valid_mask = 0U;
  uint8_t self_test_pipeline_byte0_conv3 = 0U;
  uint8_t self_test_pipeline_byte0_conv9 = 0U;
  uint8_t self_test_pipeline_byte0_conv15 = 0U;
  uint8_t self_test_pipeline_byte0_conv21 = 0U;
  uint8_t self_test_epoch_index = 0U;
  uint8_t diag_epoch_index = 0U;
  char weight_char = 'X';
  char diag_epoch_char = '0';
  char self_test_epoch_char = '0';
  char top_class_char = 'D';

  if ((buffer == NULL) || (buffer_size == 0U))
  {
    return;
  }

  if ((CFG_THERMAL_AI_ENABLE == 0U) || (app_thermal_ai_is_enabled() == 0U))
  {
    (void)snprintf(buffer, buffer_size, "AI OFF");
    return;
  }

#if (CFG_THERMAL_AI_PREVIEW_HOTSPOT_ONLY != 0U)
  if (g_thermal_ai_model_ready == 0U)
  {
    (void)snprintf(buffer, buffer_size, "AI ON --");
    return;
  }

  if (tx_mutex_get(&g_thermal_ai_mutex, TX_NO_WAIT) == TX_SUCCESS)
  {
    ai_result_snapshot = g_thermal_ai_runtime.last_result;
    ai_result_valid = 1U;
    tx_mutex_put(&g_thermal_ai_mutex);
  }

  if ((ai_result_valid != 0U) &&
      (ai_result_snapshot.detection_count != 0U) &&
      (ai_result_snapshot.detections[0].valid != 0U))
  {
    const thermal_ai_detection_t *detection_ptr = &ai_result_snapshot.detections[0];

    (void)snprintf(buffer,
                   buffer_size,
                   "AI H%u %u %lums",
                   (unsigned int)ai_result_snapshot.detection_count,
                   (unsigned int)((detection_ptr->confidence_permille + 5U) / 10U),
                   (unsigned long)displayed_inference_ms);
  }
  else
  {
    (void)snprintf(buffer, buffer_size, "AI ON -- %lums", (unsigned long)displayed_inference_ms);
  }
  return;
#endif

  aton_irq_error_latched = g_ll_aton_rt_irq_err_latched;

  if (g_thermal_ai_iac_fault_latched != 0U)
  {
    switch (g_thermal_ai_iac_last_periph_id)
    {
      case RIF_RISC_PERIPH_INDEX_NPU:
        iac_name = "NPU";
        break;

      case RIF_RISC_PERIPH_INDEX_XSPI2:
        iac_name = "X2";
        break;

      case RIF_RISC_PERIPH_INDEX_XSPIM:
        iac_name = "XM";
        break;

      default:
        iac_name = "UNK";
        break;
    }

    (void)snprintf(buffer, buffer_size, "IAC %s %lu",
                   iac_name,
                   (unsigned long)g_thermal_ai_iac_fault_count);
    return;
  }

  if ((g_thermal_ai_self_test_done != 0U) &&
      (g_thermal_ai_self_test_pass == 0U) &&
      (g_thermal_ai_self_test_aton_irq_error_latched != 0U))
  {
    (void)snprintf(buffer,
                   buffer_size,
                   "AE%04X%04X",
                   (unsigned int)g_thermal_ai_self_test_aton_irq_error_irqs,
                   (unsigned int)g_thermal_ai_self_test_aton_irq_error_aux);
    return;
  }

  if (aton_irq_error_latched != 0U)
  {
    (void)snprintf(buffer,
                   buffer_size,
                   "AE%04X%04X",
                   (unsigned int)g_thermal_ai_diag_aton_irq_error_irqs,
                   (unsigned int)g_thermal_ai_diag_aton_irq_error_aux);
    return;
  }

  switch (g_thermal_ai_diag_stage)
  {
    case 1U:
      (void)snprintf(buffer, buffer_size, "AI GO %lu", (unsigned long)g_thermal_ai_diag_run_count);
      return;

    case 2U:
      (void)snprintf(buffer, buffer_size, "AI ION");
      return;

    case 3U:
      switch (g_thermal_ai_diag_io_reason)
      {
        case 1U:
          (void)snprintf(buffer, buffer_size, "AI IBI");
          break;

        case 2U:
          (void)snprintf(buffer, buffer_size, "AI IBO");
          break;

        case 3U:
          (void)snprintf(buffer, buffer_size, "AI IN0");
          break;

        case 4U:
          (void)snprintf(buffer, buffer_size, "AI OU0");
          break;

        case 5U:
          (void)snprintf(buffer, buffer_size, "AI IUP");
          break;

        case 6U:
          (void)snprintf(buffer, buffer_size, "AI OUP");
          break;

        default:
          (void)snprintf(buffer, buffer_size, "AI IOB");
          break;
      }
      return;

    case 4U:
      (void)snprintf(buffer, buffer_size, "AI IOL");
      return;

    case 5U:
      (void)snprintf(buffer, buffer_size, "AI ORI");
      return;

    case 6U:
      (void)snprintf(buffer, buffer_size, "AI BG");
      return;

    case 7U:
      (void)snprintf(buffer, buffer_size, "AI TST");
      return;

    case 8U:
      (void)snprintf(buffer, buffer_size, "AI QTZ");
      return;

    case 9U:
      (void)snprintf(buffer, buffer_size, "AI CLN");
      return;

    case 10U:
      (void)snprintf(buffer, buffer_size, "AI RUN %lu", (unsigned long)g_thermal_ai_diag_run_count);
      return;

    case 11U:
      (void)snprintf(buffer, buffer_size, "AI RET %lu", (unsigned long)g_thermal_ai_diag_run_count);
      return;

    case 12U:
      (void)snprintf(buffer, buffer_size, "AI DEC %lu", (unsigned long)g_thermal_ai_diag_run_count);
      return;

    case 13U:
      (void)snprintf(buffer, buffer_size, "AI NCW");
      return;

    case 14U:
      (void)snprintf(buffer, buffer_size, "AI NCF");
      return;

    default:
      break;
  }

  if (g_thermal_ai_model_ready == 0U)
  {
    (void)snprintf(buffer, buffer_size, "AI ARM");
    return;
  }

  if (tx_mutex_get(&g_thermal_ai_mutex, TX_NO_WAIT) == TX_SUCCESS)
  {
    ai_result_snapshot = g_thermal_ai_runtime.last_result;
    ai_result_valid = 1U;
    max_score_permille = g_thermal_ai_diag_max_score_permille;
    max_objectness_permille = g_thermal_ai_diag_max_objectness_permille;
    diag_epoch_index = g_thermal_ai_diag_epoch_index;
    self_test_done = g_thermal_ai_self_test_done;
    self_test_pass = g_thermal_ai_self_test_pass;
    self_test_output_byte0 = g_thermal_ai_self_test_output_byte0;
    self_test_output_byte1 = g_thermal_ai_self_test_output_byte1;
    self_test_output_byte2 = g_thermal_ai_self_test_output_byte2;
    self_test_output_byte3 = g_thermal_ai_self_test_output_byte3;
    self_test_mid_valid = g_thermal_ai_self_test_mid_valid;
    self_test_mid_byte0 = g_thermal_ai_self_test_mid_byte0;
    self_test_mid_byte1 = g_thermal_ai_self_test_mid_byte1;
    self_test_mid_byte2 = g_thermal_ai_self_test_mid_byte2;
    self_test_mid_byte3 = g_thermal_ai_self_test_mid_byte3;
    self_test_pipeline_valid_mask = g_thermal_ai_self_test_pipeline_valid_mask;
    self_test_pipeline_byte0_conv3 = g_thermal_ai_self_test_pipeline_byte0_conv3;
    self_test_pipeline_byte0_conv9 = g_thermal_ai_self_test_pipeline_byte0_conv9;
    self_test_pipeline_byte0_conv15 = g_thermal_ai_self_test_pipeline_byte0_conv15;
    self_test_pipeline_byte0_conv21 = g_thermal_ai_self_test_pipeline_byte0_conv21;
    self_test_epoch_index = g_thermal_ai_self_test_epoch_index;
    weight_char = (g_thermal_ai_diag_weights_ok != 0U) ? 'W' : 'X';
    tx_mutex_put(&g_thermal_ai_mutex);
  }

  diag_epoch_char = (diag_epoch_index < 10U) ? (char)('0' + diag_epoch_index)
                                              : (char)('A' + (diag_epoch_index - 10U));
  self_test_epoch_char = (self_test_epoch_index < 10U) ? (char)('0' + self_test_epoch_index)
                                                        : (char)('A' + (self_test_epoch_index - 10U));

  if ((ai_result_valid != 0U) &&
      (ai_result_snapshot.detection_count != 0U) &&
      (ai_result_snapshot.detections[0].valid != 0U))
  {
    const thermal_ai_detection_t *status_detection_ptr = &ai_result_snapshot.detections[0];
    uint32_t detection_index;

    for (detection_index = 0U; detection_index < ai_result_snapshot.detection_count; detection_index++)
    {
      if ((ai_result_snapshot.detections[detection_index].valid != 0U) &&
          (ai_result_snapshot.detections[detection_index].class_id == (uint8_t)THERMAL_AI_CLASS_CIRCUIT_BOARD_ABNORMAL_HOTSPOT))
      {
        status_detection_ptr = &ai_result_snapshot.detections[detection_index];
        break;
      }
    }

    switch ((thermal_ai_class_t)status_detection_ptr->class_id)
    {
      case THERMAL_AI_CLASS_CIRCUIT_BOARD_NORMAL:
        top_class_char = 'N';
        break;

      case THERMAL_AI_CLASS_CIRCUIT_BOARD_ABNORMAL_HOTSPOT:
        top_class_char = 'H';
        break;

      default:
        top_class_char = 'D';
        break;
    }

    (void)snprintf(buffer,
                   buffer_size,
                   "AI %c%u %u %lums",
                   top_class_char,
                   (unsigned int)ai_result_snapshot.detection_count,
                   (unsigned int)((status_detection_ptr->confidence_permille + 5U) / 10U),
                   (unsigned long)displayed_inference_ms);
    return;
  }

  if (self_test_done != 0U)
  {
    if (self_test_pass == 0U)
    {
      if (self_test_pipeline_valid_mask == 0x0FU)
      {
        (void)snprintf(buffer,
                       buffer_size,
                       "P%02X%02X%02X%02X",
                       (unsigned int)self_test_pipeline_byte0_conv3,
                       (unsigned int)self_test_pipeline_byte0_conv9,
                       (unsigned int)self_test_pipeline_byte0_conv15,
                       (unsigned int)self_test_pipeline_byte0_conv21);
      }
      else if (self_test_mid_valid != 0U)
      {
        (void)snprintf(buffer,
                       buffer_size,
                       "Q%02X%02X%02X%02X",
                       (unsigned int)self_test_mid_byte0,
                       (unsigned int)self_test_mid_byte1,
                       (unsigned int)self_test_mid_byte2,
                       (unsigned int)self_test_mid_byte3);
      }
      else
      {
        (void)snprintf(buffer,
                       buffer_size,
                       "%c%c%02X%02X%02X%02X",
                       weight_char,
                       self_test_epoch_char,
                       (unsigned int)self_test_output_byte0,
                       (unsigned int)self_test_output_byte1,
                       (unsigned int)self_test_output_byte2,
                       (unsigned int)self_test_output_byte3);
      }
    }
    else
    {
      (void)snprintf(buffer,
                     buffer_size,
                     "%cT%u S%u O%u",
                     weight_char,
                     (unsigned int)self_test_pass,
                     (unsigned int)((max_score_permille + 5U) / 10U),
                     (unsigned int)((max_objectness_permille + 5U) / 10U));
    }
  }
  else
  {
    (void)snprintf(buffer, buffer_size, "%c%c TEST", weight_char, diag_epoch_char);
  }
}

/**
  * @brief  Update the AI diagnostic stage and optionally yield once so GUI can paint it.
  * @param  stage New stage code.
  * @param  allow_settle Non-zero to sleep briefly on the first AI attempt only.
  * @retval None
  */
static void app_threadx_thermal_ai_set_diag_stage(uint8_t stage, uint8_t allow_settle)
{
  g_thermal_ai_diag_stage = stage;

  if ((allow_settle != 0U) && (g_thermal_ai_diag_run_count == 1U))
  {
    tx_thread_sleep((CFG_THERMAL_AI_DIAG_STAGE_SETTLE_TICKS > 0U) ? CFG_THERMAL_AI_DIAG_STAGE_SETTLE_TICKS : 1U);
  }
}

/**
  * @brief  Compute one compact XOR signature over a signed int8 buffer.
  * @param  buffer Pointer to the buffer.
  * @param  length Buffer length in bytes.
  * @retval uint8_t XOR signature over the raw byte values.
  */
static uint8_t __attribute__((unused)) app_threadx_thermal_ai_calc_xor8(const int8_t *buffer, uint32_t length)
{
  uint32_t index;
  uint8_t xor_value = 0U;

  if (buffer == NULL)
  {
    return 0U;
  }

  for (index = 0U; index < length; index++)
  {
    xor_value ^= (uint8_t)buffer[index];
  }

  return xor_value;
}

/**
  * @brief  Find one generated AI buffer descriptor by its exact debug name.
  * @param  buffer_list_ptr Buffer-info list returned by CubeAI.
  * @param  buffer_name_ptr Expected buffer name.
  * @retval Matching descriptor pointer, or NULL when not found.
  */
static const LL_Buffer_InfoTypeDef *app_threadx_thermal_ai_find_buffer_by_name(
    const LL_Buffer_InfoTypeDef *buffer_list_ptr,
    const char *buffer_name_ptr)
{
  if ((buffer_list_ptr == NULL) || (buffer_name_ptr == NULL))
  {
    return NULL;
  }

  while (buffer_list_ptr->name != NULL)
  {
    if (strcmp(buffer_list_ptr->name, buffer_name_ptr) == 0)
    {
      return buffer_list_ptr;
    }
    buffer_list_ptr++;
  }

  return NULL;
}

/**
  * @brief  Capture the first raw byte of one generated AI buffer.
  * @param  buffer_info_ptr Generated buffer descriptor.
  * @param  byte0_ptr Output byte pointer.
  * @retval uint8_t 1 when the first byte was captured, otherwise 0.
  */
static uint8_t app_threadx_thermal_ai_capture_buffer_byte0(const LL_Buffer_InfoTypeDef *buffer_info_ptr,
                                                           uint8_t *byte0_ptr)
{
  uint8_t *buffer_ptr;
  uint32_t buffer_len;

  if ((buffer_info_ptr == NULL) || (byte0_ptr == NULL))
  {
    return 0U;
  }

  buffer_ptr = (uint8_t *)LL_Buffer_addr_start(buffer_info_ptr);
  buffer_len = LL_Buffer_len(buffer_info_ptr);
  if ((buffer_ptr == NULL) || (buffer_len == 0U))
  {
    return 0U;
  }

  app_threadx_dcache_invalidate_by_addr(buffer_ptr, buffer_len);
  *byte0_ptr = buffer_ptr[0];
  return 1U;
}

/**
  * @brief  Capture the first four raw bytes of one generated AI buffer.
  * @param  buffer_info_ptr Generated buffer descriptor.
  * @param  byte0_ptr Output byte 0 pointer.
  * @param  byte1_ptr Output byte 1 pointer.
  * @param  byte2_ptr Output byte 2 pointer.
  * @param  byte3_ptr Output byte 3 pointer.
  * @retval uint8_t 1 when four bytes were captured, otherwise 0.
  */
static uint8_t app_threadx_thermal_ai_capture_buffer_bytes(const LL_Buffer_InfoTypeDef *buffer_info_ptr,
                                                           uint8_t *byte0_ptr,
                                                           uint8_t *byte1_ptr,
                                                           uint8_t *byte2_ptr,
                                                           uint8_t *byte3_ptr)
{
  uint8_t *buffer_ptr;
  uint32_t buffer_len;

  if ((buffer_info_ptr == NULL) || (byte0_ptr == NULL) || (byte1_ptr == NULL) ||
      (byte2_ptr == NULL) || (byte3_ptr == NULL))
  {
    return 0U;
  }

  buffer_ptr = (uint8_t *)LL_Buffer_addr_start(buffer_info_ptr);
  buffer_len = LL_Buffer_len(buffer_info_ptr);
  if ((buffer_ptr == NULL) || (buffer_len < 4U))
  {
    return 0U;
  }

  app_threadx_dcache_invalidate_by_addr(buffer_ptr, buffer_len);
  *byte0_ptr = buffer_ptr[0];
  *byte1_ptr = buffer_ptr[1];
  *byte2_ptr = buffer_ptr[2];
  *byte3_ptr = buffer_ptr[3];
  return 1U;
}

/**
  * @brief  Capture a few raw q7 output signature values for on-device diagnostics.
  * @param  buffer Pointer to the q7 output buffer.
  * @param  length Buffer length in bytes.
  * @param  byte0_ptr Output pointer for byte 0.
  * @param  min_u8_ptr Output pointer for the minimum raw byte.
  * @param  max_u8_ptr Output pointer for the maximum raw byte.
  * @retval None
  */
static void __attribute__((unused)) app_threadx_thermal_ai_capture_output_signature(const int8_t *buffer,
                                                            uint32_t length,
                                                            uint8_t *byte0_ptr,
                                                            uint8_t *min_u8_ptr,
                                                            uint8_t *max_u8_ptr)
{
  uint32_t index;
  int8_t min_value;
  int8_t max_value;

  if ((buffer == NULL) || (length == 0U))
  {
    if (byte0_ptr != NULL)
    {
      *byte0_ptr = 0U;
    }
    if (min_u8_ptr != NULL)
    {
      *min_u8_ptr = 0U;
    }
    if (max_u8_ptr != NULL)
    {
      *max_u8_ptr = 0U;
    }
    return;
  }

  min_value = buffer[0];
  max_value = buffer[0];

  if (byte0_ptr != NULL)
  {
    *byte0_ptr = (uint8_t)buffer[0];
  }

  for (index = 1U; index < length; index++)
  {
    if (buffer[index] < min_value)
    {
      min_value = buffer[index];
    }
    if (buffer[index] > max_value)
    {
      max_value = buffer[index];
    }
  }

  if (min_u8_ptr != NULL)
  {
    *min_u8_ptr = (uint8_t)min_value;
  }
  if (max_u8_ptr != NULL)
  {
    *max_u8_ptr = (uint8_t)max_value;
  }
}

/**
  * @brief  Capture one IAC illegal-access event raised by RIF.
  * @param  PeriphId RIF peripheral identifier that triggered the fault.
  * @retval None
  */
void HAL_RIF_ILA_Callback(uint32_t PeriphId)
{
  g_thermal_ai_iac_last_periph_id = PeriphId;
  g_thermal_ai_iac_fault_count++;
  g_thermal_ai_iac_fault_latched = 1U;
  g_thermal_ai_model_ready = 0U;

  HAL_RIF_IAC_DisableIT(PeriphId);
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

#if (CFG_THERMAL_AI_RGB565_BOX_COMPOSITE != 0U)
    /* Rectangles are baked into the complete image frame. No independent
     * LVGL object is moved or invalidated in this mode. */
#else
    app_threadx_gui_update_ai_overlay_set(g_thermal_ai_preview_boxes,
                                          g_thermal_ai_preview_labels,
                                          (int32_t)lv_obj_get_x(ui->WidgetsDemo_preview_img),
                                          (int32_t)lv_obj_get_y(ui->WidgetsDemo_preview_img),
                                          CFG_GUI_PREVIEW_WIDTH,
                                          CFG_GUI_PREVIEW_HEIGHT,
                                          ((app_thermal_ai_is_enabled() != 0U) && (ai_result_valid != 0U)) ? &ai_result_snapshot : NULL,
                                          ai_hide_unconfirmed_abnormal);
    app_threadx_gui_update_ai_overlay_set(g_thermal_ai_fullscreen_boxes,
                                          g_thermal_ai_fullscreen_labels,
                                          (int32_t)lv_obj_get_x(ui->WidgetsDemo_fullscreen_preview_img),
                                          (int32_t)lv_obj_get_y(ui->WidgetsDemo_fullscreen_preview_img),
                                          CFG_GUI_FULLSCREEN_WIDTH,
                                          CFG_GUI_FULLSCREEN_HEIGHT,
                                          ((app_thermal_ai_is_enabled() != 0U) && (ai_result_valid != 0U)) ? &ai_result_snapshot : NULL,
                                          ai_hide_unconfirmed_abnormal);
#endif
  }

  return abnormal_detected;
}

/**
  * @brief  Refresh the full-screen image buffer from the latest preview frame.
  * @param  None
  * @retval None
  */
static void app_threadx_gui_update_fullscreen_image(void)
{
  const uint16_t *preview_rgb565_frame;

  preview_rgb565_frame = tiny1c_thermal_app_get_rgb565_frame();
  if (preview_rgb565_frame == NULL)
  {
    return;
  }

  app_threadx_gui_build_scaled_frame(preview_rgb565_frame,
                                      g_gui_fullscreen_rgb565_frame,
                                      tiny1c_thermal_app_get_preview_width(),
                                      tiny1c_thermal_app_get_preview_height(),
                                      CFG_GUI_FULLSCREEN_WIDTH,
                                      CFG_GUI_FULLSCREEN_HEIGHT);
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

  for (dest_y = 0U; dest_y < dest_height; dest_y++)
  {
    uint32_t src_y = (((dest_y * 2U) + 1U) * (uint32_t)source_height) / ((uint32_t)dest_height * 2U);
    uint32_t dest_row_index = dest_y * (uint32_t)dest_width;
    uint32_t src_row_index;

    if (src_y >= source_height)
    {
      src_y = (uint32_t)source_height - 1U;
    }
    src_row_index = src_y * (uint32_t)source_width;

    for (dest_x = 0U; dest_x < dest_width; dest_x++)
    {
      uint32_t src_x = (((dest_x * 2U) + 1U) * (uint32_t)source_width) / ((uint32_t)dest_width * 2U);
      if (src_x >= source_width)
      {
        src_x = (uint32_t)source_width - 1U;
      }
      dest_frame[dest_row_index + dest_x] = source_frame[src_row_index + src_x];
    }
  }
}
/**
  * @brief  Return whether the compiled thermal network buffers are ready for inference.
  * @retval uint8_t 1 when input/output buffers are ready, otherwise 0.
  */
static uint8_t app_threadx_thermal_ai_prepare_io(void)
{
  unsigned char *input_base_raw;
  unsigned char *output_base_raw;
  uint8_t needs_refresh_output_info = 0U;

  g_thermal_ai_diag_io_reason = 0U;

  if ((g_thermal_ai_input_buffer != NULL) && (g_thermal_ai_output_buffer != NULL))
  {
    return 1U;
  }

  g_thermal_ai_input_info = LL_ATON_Input_Buffers_Info_thermal();
  g_thermal_ai_output_info = LL_ATON_Output_Buffers_Info_thermal();
  if ((g_thermal_ai_input_info == NULL) || (g_thermal_ai_output_info == NULL))
  {
    app_threadx_thermal_ai_set_diag_stage(2U, 1U);
    return 0U;
  }

  if (g_thermal_ai_user_io_bound == 0U)
  {
#if ((CFG_THERMAL_AI_GENERATED_USER_INPUTS != 0U) || (CFG_THERMAL_AI_GENERATED_USER_OUTPUTS != 0U))
#if (CFG_THERMAL_AI_GENERATED_USER_INPUTS != 0U)
    if (LL_ATON_Set_User_Input_Buffer_thermal(0U,
                                              (void *)g_thermal_ai_user_input_storage,
                                              sizeof(g_thermal_ai_user_input_storage)) != LL_ATON_User_IO_NOERROR)
    {
      g_thermal_ai_diag_io_reason = 1U;
      app_threadx_thermal_ai_set_diag_stage(3U, 1U);
      return 0U;
    }
#endif

#if (CFG_THERMAL_AI_GENERATED_USER_OUTPUTS != 0U)
    if (LL_ATON_Set_User_Output_Buffer_thermal(0U,
                                               (void *)g_thermal_ai_user_output_storage,
                                               sizeof(g_thermal_ai_user_output_storage)) != LL_ATON_User_IO_NOERROR)
    {
      g_thermal_ai_diag_io_reason = 2U;
      app_threadx_thermal_ai_set_diag_stage(3U, 1U);
      return 0U;
    }
#endif
#endif

    g_thermal_ai_user_io_bound = 1U;
    needs_refresh_output_info = 1U;
  }

  if (needs_refresh_output_info != 0U)
  {
    g_thermal_ai_output_info = LL_ATON_Output_Buffers_Info_thermal();
    if (g_thermal_ai_output_info == NULL)
    {
      g_thermal_ai_diag_io_reason = 7U;
      app_threadx_thermal_ai_set_diag_stage(3U, 1U);
      return 0U;
    }
  }

  input_base_raw = g_thermal_ai_input_info[0].addr_base.p;
  output_base_raw = g_thermal_ai_output_info[0].addr_base.p;

  if ((CFG_THERMAL_AI_GENERATED_USER_INPUTS == 0U) &&
      (g_thermal_ai_input_info[0].is_user_allocated != 0U))
  {
    g_thermal_ai_diag_io_reason = 5U;
    app_threadx_thermal_ai_set_diag_stage(3U, 1U);
    return 0U;
  }

  if ((CFG_THERMAL_AI_GENERATED_USER_OUTPUTS == 0U) &&
      (g_thermal_ai_output_info[0].is_user_allocated != 0U))
  {
    g_thermal_ai_diag_io_reason = 6U;
    app_threadx_thermal_ai_set_diag_stage(3U, 1U);
    return 0U;
  }

  g_thermal_ai_input_buffer = (int8_t *)(void *)LL_Buffer_addr_start(&g_thermal_ai_input_info[0]);
  g_thermal_ai_output_buffer = (int8_t *)(void *)LL_Buffer_addr_start(&g_thermal_ai_output_info[0]);
  if (g_thermal_ai_input_buffer == NULL)
  {
    g_thermal_ai_diag_io_reason = 3U;
    app_threadx_thermal_ai_set_diag_stage(3U, 1U);
    return 0U;
  }

  if (g_thermal_ai_output_buffer == NULL)
  {
    g_thermal_ai_diag_io_reason = 4U;
    app_threadx_thermal_ai_set_diag_stage(3U, 1U);
    return 0U;
  }

  TX_PARAMETER_NOT_USED(input_base_raw);
  TX_PARAMETER_NOT_USED(output_base_raw);

  if ((LL_Buffer_len(&g_thermal_ai_input_info[0]) != CFG_THERMAL_AI_INPUT_PIXELS) ||
      (LL_Buffer_len(&g_thermal_ai_output_info[0]) != CFG_THERMAL_AI_OUTPUT_BYTES))
  {
    app_threadx_thermal_ai_set_diag_stage(4U, 1U);
    g_thermal_ai_input_buffer = NULL;
    g_thermal_ai_output_buffer = NULL;
    return 0U;
  }

  return 1U;
}

/**
  * @brief  Load one fixed reference int8 input tensor for board-side AI self-test.
  * @param  background_temp14_ptr Optional output for background temp14 metadata.
  * @param  max_temp14_ptr Optional output for max temp14 metadata.
  * @retval uint8_t 1 when the reference tensor was copied, otherwise 0.
  */
static uint8_t __attribute__((unused)) app_threadx_thermal_ai_load_reference_input(uint16_t *background_temp14_ptr,
                                                           uint16_t *max_temp14_ptr)
{
#if ((CFG_THERMAL_AI_USE_REFERENCE_INPUT != 0U) || (CFG_THERMAL_AI_SELF_TEST_ENABLE != 0U))
  uint32_t reference_len;

  if (g_thermal_ai_input_buffer == NULL)
  {
    return 0U;
  }

  reference_len = (uint32_t)(g_thermal_ai_reference_input_end - g_thermal_ai_reference_input);
  if (reference_len != CFG_THERMAL_AI_INPUT_PIXELS)
  {
    return 0U;
  }

  (void)memcpy(g_thermal_ai_input_buffer, g_thermal_ai_reference_input, reference_len);
  g_thermal_ai_self_test_input_xor = app_threadx_thermal_ai_calc_xor8(g_thermal_ai_input_buffer, reference_len);
  if (background_temp14_ptr != NULL)
  {
    *background_temp14_ptr = 0U;
  }
  if (max_temp14_ptr != NULL)
  {
    *max_temp14_ptr = 0U;
  }
  return 1U;
#else
  TX_PARAMETER_NOT_USED(background_temp14_ptr);
  TX_PARAMETER_NOT_USED(max_temp14_ptr);
  return 0U;
#endif
}

/**
  * @brief  Probe signatures distributed across the external weight blob.
  * @retval uint8_t 1 when all sampled bytes match the generated weight file.
  */
static uint8_t app_threadx_thermal_ai_probe_weight_signature(void)
{
  volatile const uint8_t *weight_ptr = (volatile const uint8_t *)CFG_THERMAL_AI_WEIGHT_BLOB_ADDR;
  static uint8_t s_weight_hash_checked = 0U;
  static uint8_t s_weight_hash_ok = 0U;
  uint32_t weight_index;
  uint32_t fnv1a32 = 0x811C9DC5UL;

  if (app_threadx_thermal_ai_prepare_embedded_weights() == 0U)
  {
    return 0U;
  }

  if (s_weight_hash_checked != 0U)
  {
    return s_weight_hash_ok;
  }

  for (weight_index = 0U; weight_index < CFG_THERMAL_AI_WEIGHT_BLOB_BYTES; weight_index++)
  {
    fnv1a32 ^= (uint32_t)weight_ptr[weight_index];
    fnv1a32 *= 0x01000193UL;
  }

  s_weight_hash_ok = (fnv1a32 == CFG_THERMAL_AI_WEIGHT_BLOB_FNV1A32) ? 1U : 0U;
  s_weight_hash_checked = 1U;

  return s_weight_hash_ok;
}

/**
  * @brief  Copy the embedded thermal weight blob into the execution RAM pool once.
  * @retval uint8_t 1 when the runtime weight blob is ready in RAM, otherwise 0.
  */
static uint8_t app_threadx_thermal_ai_prepare_embedded_weights(void)
{
#if (CFG_THERMAL_AI_WEIGHT_BACKING_XSPI2 != 0U)
  return 1U;
#else
  uint32_t weight_len;
  uint8_t *dest_ptr = (uint8_t *)CFG_THERMAL_AI_WEIGHT_BLOB_ADDR;

  if (g_thermal_ai_weight_blob_loaded != 0U)
  {
    return 1U;
  }

  weight_len = (uint32_t)(g_thermal_ai_weight_blob_end - g_thermal_ai_weight_blob);
  if (weight_len != CFG_THERMAL_AI_WEIGHT_BLOB_BYTES)
  {
    return 0U;
  }

  (void)memcpy(dest_ptr, g_thermal_ai_weight_blob, weight_len);
  app_threadx_dcache_clean_by_addr(dest_ptr, CFG_THERMAL_AI_WEIGHT_BLOB_CACHE_BYTES);
  app_threadx_dcache_invalidate_by_addr(dest_ptr, CFG_THERMAL_AI_WEIGHT_BLOB_CACHE_BYTES);
  g_thermal_ai_weight_blob_loaded = 1U;
  return 1U;
#endif
}

/**
  * @brief  Perform one-time CACHEAXI clean+invalidate on the xSPI2 weight blob.
  * @retval uint8_t 1 when the command completed successfully, otherwise 0.
  */
static uint8_t app_threadx_thermal_ai_prepare_weight_cache(void)
{
  static uint8_t s_weight_cache_prepared = 0U;

#if (CFG_THERMAL_AI_DIAG_DISABLE_NPU_CACHE != 0U)
  s_weight_cache_prepared = 1U;
  return 1U;
#endif

  if (s_weight_cache_prepared != 0U)
  {
    return 1U;
  }

  if (app_threadx_thermal_ai_prepare_embedded_weights() == 0U)
  {
    return 0U;
  }

  /* The xSPI2 weight blob is programmed out-of-band, so invalidate CACHEAXI
     once before the first inference to avoid stale NPU cache lines. */
#if (CFG_THERMAL_AI_WEIGHT_BACKING_XSPI2 != 0U)
  LL_ATON_Cache_NPU_Invalidate();
#endif

  s_weight_cache_prepared = 1U;
  return 1U;
}

/**
  * @brief  Initialize the low-level ATON runtime once for repeated inferences.
  * @retval uint8_t 1 when ready, otherwise 0.
  */
static uint8_t app_threadx_thermal_ai_prepare_runtime(void)
{
  if (g_thermal_ai_ll_runtime_ready != 0U)
  {
    return 1U;
  }

  LL_ATON_RT_ClearIrqErrorState();
  LL_ATON_RT_RuntimeInit();
  LL_ATON_RT_SetNetworkCallback(&NN_Instance_thermal, app_threadx_thermal_ai_epoch_callback);
  LL_ATON_RT_Init_Network(&NN_Instance_thermal);

  g_thermal_ai_ll_runtime_ready = 1U;
  g_thermal_ai_ll_network_needs_reset = 0U;
  return 1U;
}

/**
  * @brief  Tear down the low-level ATON runtime after a fatal board-side AI error.
  * @retval None
  */
static void app_threadx_thermal_ai_release_runtime(void)
{
  if (g_thermal_ai_ll_runtime_ready == 0U)
  {
    return;
  }

  LL_ATON_RT_SetNetworkCallback(&NN_Instance_thermal, NULL);
  LL_ATON_RT_DeInit_Network(&NN_Instance_thermal);
  LL_ATON_RT_RuntimeDeInit();

  g_thermal_ai_ll_runtime_ready = 0U;
  g_thermal_ai_ll_network_needs_reset = 0U;
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
  * @brief  Build one AI-only temp14 view with conservative cold-pit correction.
  * @param  source_frame Pointer to the raw temp14 frame.
  * @param  dest_frame Pointer to the corrected temp14 frame.
  * @retval None
  */
static void app_threadx_thermal_ai_build_corrected_temp14_frame(const uint16_t *source_frame,
                                                                uint16_t *dest_frame)
{
  uint32_t pixel_count;
  uint32_t y_index;
  uint32_t x_index;

  if ((source_frame == NULL) || (dest_frame == NULL))
  {
    return;
  }

  pixel_count = (uint32_t)CFG_THERMAL_AI_INPUT_WIDTH * (uint32_t)CFG_THERMAL_AI_INPUT_HEIGHT;
  (void)memcpy(dest_frame, source_frame, pixel_count * sizeof(uint16_t));

#if (CFG_THERMAL_AI_COLD_PIT_CORRECTION_ENABLE != 0U)
  for (y_index = 1U; y_index + 1U < CFG_THERMAL_AI_INPUT_HEIGHT; y_index++)
  {
    for (x_index = 1U; x_index + 1U < CFG_THERMAL_AI_INPUT_WIDTH; x_index++)
    {
      uint32_t pixel_index = y_index * CFG_THERMAL_AI_INPUT_WIDTH + x_index;
      uint16_t center = source_frame[pixel_index];
      uint16_t ring_values[8];
      uint16_t neighbor_min;
      uint16_t neighbor_max;
      uint32_t hot_neighbor_count = 0U;
      uint32_t ring_index;

      ring_values[0] = source_frame[pixel_index - CFG_THERMAL_AI_INPUT_WIDTH - 1U];
      ring_values[1] = source_frame[pixel_index - CFG_THERMAL_AI_INPUT_WIDTH];
      ring_values[2] = source_frame[pixel_index - CFG_THERMAL_AI_INPUT_WIDTH + 1U];
      ring_values[3] = source_frame[pixel_index - 1U];
      ring_values[4] = source_frame[pixel_index + 1U];
      ring_values[5] = source_frame[pixel_index + CFG_THERMAL_AI_INPUT_WIDTH - 1U];
      ring_values[6] = source_frame[pixel_index + CFG_THERMAL_AI_INPUT_WIDTH];
      ring_values[7] = source_frame[pixel_index + CFG_THERMAL_AI_INPUT_WIDTH + 1U];

      if ((center < CFG_THERMAL_AI_VALID_TEMP14_MIN) || (center > CFG_THERMAL_AI_VALID_TEMP14_MAX))
      {
        continue;
      }

      neighbor_min = ring_values[0];
      neighbor_max = ring_values[0];
      for (ring_index = 0U; ring_index < 8U; ring_index++)
      {
        uint16_t ring_value = ring_values[ring_index];

        if ((ring_value < CFG_THERMAL_AI_VALID_TEMP14_MIN) || (ring_value > CFG_THERMAL_AI_VALID_TEMP14_MAX))
        {
          hot_neighbor_count = 0U;
          break;
        }

        if (ring_value < neighbor_min) neighbor_min = ring_value;
        if (ring_value > neighbor_max) neighbor_max = ring_value;
        if (ring_value >= (uint16_t)(center + CFG_THERMAL_AI_COLD_PIT_DELTA_TEMP14)) hot_neighbor_count++;
      }

      if ((hot_neighbor_count < CFG_THERMAL_AI_COLD_PIT_MIN_NEIGHBORS) ||
          ((uint16_t)(neighbor_max - neighbor_min) > CFG_THERMAL_AI_COLD_PIT_NEIGHBOR_SPAN_MAX))
      {
        continue;
      }

      dest_frame[pixel_index] = (uint16_t)(neighbor_max + CFG_THERMAL_AI_COLD_PIT_HOTTER_MARGIN);
      if (dest_frame[pixel_index] > CFG_THERMAL_AI_VALID_TEMP14_MAX)
      {
        dest_frame[pixel_index] = CFG_THERMAL_AI_VALID_TEMP14_MAX;
      }
    }
  }
#endif
}

/**
  * @brief  Build one preview-oriented AI input tensor from the latest raw temp14 frame.
  * @param  temp14_frame Pointer to the latest raw temp14 frame.
  * @param  background_temp14_ptr Output median/background temp14 pointer.
  * @param  max_temp14_ptr Output frame maximum temp14 pointer.
  * @retval uint8_t 1 when an AI input tensor was built, otherwise 0.
  */
static uint8_t app_threadx_thermal_ai_build_input(const uint16_t *temp14_frame,
                                                  uint16_t *background_temp14_ptr,
                                                  uint16_t *max_temp14_ptr)
{
  uint32_t pixel_index = 0U;
  uint32_t valid_pixel_count = 0U;
  uint32_t invalid_pixel_count = 0U;
  uint32_t invalid_pixel_permille;
  uint16_t background_temp14;
  uint16_t max_temp14 = 0U;
  float input_scale;
  int16_t input_zero_point;
  uint32_t y_out;
  uint32_t x_out;
  volatile int8_t input_probe;

  if ((temp14_frame == NULL) || (g_thermal_ai_input_buffer == NULL))
  {
    return 0U;
  }

  (void)memset(g_thermal_ai_histogram, 0, sizeof(g_thermal_ai_histogram));
  app_threadx_thermal_ai_set_diag_stage(5U, 1U);

  for (y_out = 0U; y_out < CFG_THERMAL_AI_INPUT_HEIGHT; y_out++)
  {
    for (x_out = 0U; x_out < CFG_THERMAL_AI_INPUT_WIDTH; x_out++)
    {
      uint16_t temp14_value = app_threadx_temp14_get_preview_oriented_sample(temp14_frame,
                                                                              CFG_THERMAL_AI_INPUT_WIDTH,
                                                                              CFG_THERMAL_AI_INPUT_HEIGHT,
                                                                              (uint16_t)x_out,
                                                                              (uint16_t)y_out);

      uint16_t histogram_value = (temp14_value < (CFG_THERMAL_AI_HISTOGRAM_BINS - 1U))
                                   ? temp14_value
                                   : (uint16_t)(CFG_THERMAL_AI_HISTOGRAM_BINS - 1U);

      if ((temp14_value < CFG_THERMAL_AI_VALID_TEMP14_MIN) ||
          (temp14_value > CFG_THERMAL_AI_VALID_TEMP14_MAX))
      {
        g_thermal_ai_oriented_temp14[pixel_index++] = 0U;
        invalid_pixel_count++;
        continue;
      }

      g_thermal_ai_oriented_temp14[pixel_index++] = temp14_value;
      g_thermal_ai_histogram[histogram_value]++;
      valid_pixel_count++;
      if (temp14_value > max_temp14)
      {
        max_temp14 = temp14_value;
      }
    }
  }

  if (valid_pixel_count == 0U)
  {
    return 0U;
  }

  invalid_pixel_permille = (invalid_pixel_count * 1000U + (CFG_THERMAL_AI_INPUT_PIXELS / 2U)) /
                           CFG_THERMAL_AI_INPUT_PIXELS;
  if (invalid_pixel_permille > CFG_THERMAL_AI_INVALID_PIXEL_MAX_PERMILLE)
  {
    return 0U;
  }

  background_temp14 = app_threadx_thermal_ai_percentile_from_histogram(valid_pixel_count);
  input_scale = (g_thermal_ai_input_info[0].scale != NULL) ? g_thermal_ai_input_info[0].scale[0] : (1.0f / 255.0f);
  input_zero_point = (g_thermal_ai_input_info[0].offset != NULL) ? g_thermal_ai_input_info[0].offset[0] : -128;
  app_threadx_thermal_ai_set_diag_stage(6U, 1U);

  /* Probe the first CPU access to the generated NPU input buffer explicitly. */
  app_threadx_thermal_ai_set_diag_stage(7U, 1U);
  input_probe = g_thermal_ai_input_buffer[0];
  g_thermal_ai_input_buffer[0] = input_probe;
  app_threadx_thermal_ai_set_diag_stage(8U, 1U);

  for (pixel_index = 0U; pixel_index < CFG_THERMAL_AI_INPUT_PIXELS; pixel_index++)
  {
    uint16_t temp14_value = g_thermal_ai_oriented_temp14[pixel_index];
    float delta_temp14;
    float normalized;
    float quantized;
    int32_t quantized_i32;

    if ((temp14_value < CFG_THERMAL_AI_VALID_TEMP14_MIN) ||
        (temp14_value > CFG_THERMAL_AI_VALID_TEMP14_MAX))
    {
      temp14_value = background_temp14;
    }

    delta_temp14 = (float)((int32_t)temp14_value - (int32_t)background_temp14);
    normalized = (delta_temp14 - CFG_THERMAL_AI_DELTA_TEMP14_MIN) / CFG_THERMAL_AI_DELTA_TEMP14_SPAN;

    if (normalized < 0.0f)
    {
      normalized = 0.0f;
    }
    else if (normalized > 1.0f)
    {
      normalized = 1.0f;
    }

    quantized = (normalized / input_scale) + (float)input_zero_point;
    quantized_i32 = (int32_t)lrintf(quantized);
    if (quantized_i32 < -128)
    {
      quantized_i32 = -128;
    }
    else if (quantized_i32 > 127)
    {
      quantized_i32 = 127;
    }

    g_thermal_ai_input_buffer[pixel_index] = (int8_t)quantized_i32;
  }

  if (background_temp14_ptr != NULL)
  {
    *background_temp14_ptr = background_temp14;
  }
  if (max_temp14_ptr != NULL)
  {
    *max_temp14_ptr = max_temp14;
  }

  return 1U;
}

/**
  * @brief  Return the sigmoid activation for one floating-point value.
  * @param  value Input value.
  * @retval float Sigmoid output in range 0..1.
  */
static float app_threadx_thermal_ai_sigmoid(float value)
{
  if (value >= 0.0f)
  {
    float exp_neg = expf(-value);
    return 1.0f / (1.0f + exp_neg);
  }

  {
    float exp_pos = expf(value);
    return exp_pos / (1.0f + exp_pos);
  }
}

/**
  * @brief  Map one detector class index into the project runtime class identifier.
  * @param  class_index Zero-based detector class index.
  * @retval uint8_t Thermal AI runtime class identifier.
  */
static uint8_t app_threadx_thermal_ai_class_index_to_runtime_id(uint32_t class_index)
{
  switch (class_index)
  {
    case 0U:
      return (uint8_t)THERMAL_AI_CLASS_CIRCUIT_BOARD_ABNORMAL_HOTSPOT;
    default:
      return (uint8_t)THERMAL_AI_CLASS_NONE;
  }
}

/**
  * @brief  Return the decoded-score threshold for one detector class.
  * @param  class_index Zero-based detector class index.
  * @retval float Minimum objectness*class score.
  */
static float app_threadx_thermal_ai_score_threshold_for_class(uint32_t class_index)
{
  switch (class_index)
  {
    case 0U:
      return CFG_THERMAL_AI_ABNORMAL_SCORE_THRESHOLD;
    default:
      return CFG_THERMAL_AI_SCORE_THRESHOLD;
  }
}

/**
  * @brief  Apply lightweight class-specific geometry filters to one decoded box.
  * @param  class_id Runtime class identifier.
  * @param  x_min Box left edge in model input coordinates.
  * @param  y_min Box top edge in model input coordinates.
  * @param  x_max Box right edge in model input coordinates.
  * @param  y_max Box bottom edge in model input coordinates.
  * @retval uint8_t 1 when the box should be kept, otherwise 0.
  */
static uint8_t app_threadx_thermal_ai_box_passes_postfilter(uint8_t class_id,
                                                            float x_min,
                                                            float y_min,
                                                            float x_max,
                                                            float y_max)
{
  float box_width = x_max - x_min;
  float box_height = y_max - y_min;
#if (CFG_THERMAL_AI_ENABLE_GEOMETRY_POSTFILTER != 0U)
  float box_area;
#endif

  if ((box_width <= 0.0f) || (box_height <= 0.0f))
  {
    return 0U;
  }

#if (CFG_THERMAL_AI_ENABLE_GEOMETRY_POSTFILTER == 0U)
  (void)class_id;
  return 1U;
#else
  box_area = box_width * box_height;
  switch ((thermal_ai_class_t)class_id)
  {
    case THERMAL_AI_CLASS_CIRCUIT_BOARD_NORMAL:
      if ((box_width < CFG_THERMAL_AI_NORMAL_MIN_WIDTH_PX) ||
          (box_height < CFG_THERMAL_AI_NORMAL_MIN_HEIGHT_PX) ||
          (box_area < CFG_THERMAL_AI_NORMAL_MIN_AREA_PX))
      {
        return 0U;
      }
      break;

    case THERMAL_AI_CLASS_CIRCUIT_BOARD_ABNORMAL_HOTSPOT:
      if ((box_width < CFG_THERMAL_AI_ABNORMAL_MIN_WIDTH_PX) ||
          (box_height < CFG_THERMAL_AI_ABNORMAL_MIN_HEIGHT_PX) ||
          (box_area < CFG_THERMAL_AI_ABNORMAL_MIN_AREA_PX) ||
          (box_area > CFG_THERMAL_AI_ABNORMAL_MAX_AREA_PX))
      {
        return 0U;
      }
      break;

    default:
      return 0U;
  }

  return 1U;
#endif
}

/**
  * @brief  Read one quantized detector output value using one candidate output layout.
  * @param  grid_y Grid Y index.
  * @param  grid_x Grid X index.
  * @param  channel_index Output-channel index.
  * @param  channel_planar_layout Non-zero for channel-major planar layout.
  * @retval int8_t Raw quantized output value.
  */
static int8_t app_threadx_thermal_ai_read_output_q7(uint32_t grid_y,
                                                    uint32_t grid_x,
                                                    uint32_t channel_index,
                                                    uint8_t channel_planar_layout)
{
  uint32_t linear_index;

  if (channel_planar_layout != 0U)
  {
    linear_index = (channel_index * CFG_THERMAL_AI_GRID_HEIGHT * CFG_THERMAL_AI_GRID_WIDTH) +
                   (grid_y * CFG_THERMAL_AI_GRID_WIDTH) +
                   grid_x;
  }
  else
  {
    linear_index = ((grid_y * CFG_THERMAL_AI_GRID_WIDTH) + grid_x) * CFG_THERMAL_AI_OUTPUT_CHANNELS +
                   channel_index;
  }

  return g_thermal_ai_output_buffer[linear_index];
}

/**
  * @brief  Determine the generated detector output memory layout.
  * @retval uint8_t 1 for BCHW planar data, 0 for BHWC interleaved data.
  */
static uint8_t app_threadx_thermal_ai_output_is_planar(void)
{
  const LL_Buffer_InfoTypeDef *output_info_ptr = g_thermal_ai_output_info;

  if ((output_info_ptr == NULL) || (output_info_ptr[0].mem_shape == NULL))
  {
    return 0U;
  }

  if (output_info_ptr[0].mem_ndims == 4U)
  {
    const uint32_t *mem_shape = output_info_ptr[0].mem_shape;

    if ((mem_shape[1] == CFG_THERMAL_AI_GRID_HEIGHT) &&
        (mem_shape[2] == CFG_THERMAL_AI_GRID_WIDTH) &&
        (mem_shape[3] == CFG_THERMAL_AI_OUTPUT_CHANNELS))
    {
      return 0U;
    }

    if ((mem_shape[1] == CFG_THERMAL_AI_OUTPUT_CHANNELS) &&
        (mem_shape[2] == CFG_THERMAL_AI_GRID_HEIGHT) &&
        (mem_shape[3] == CFG_THERMAL_AI_GRID_WIDTH))
    {
      return 1U;
    }
  }

  return (output_info_ptr[0].chpos == CHPos_First) ? 1U : 0U;
}

/**
  * @brief  Decode one candidate output layout into the temporary candidate list.
  * @param  output_scale Output tensor dequant scale.
  * @param  output_zero_point Output tensor dequant zero point.
  * @param  channel_planar_layout Non-zero for channel-major planar layout.
  * @param  raw_candidate_count_ptr Output pointer for pre-filter candidate count.
  * @param  max_score_permille_ptr Output pointer for best detection score in permille.
  * @param  max_objectness_permille_ptr Output pointer for best objectness score in permille.
  * @retval uint32_t Candidate count kept after class thresholding and geometric filtering.
  */
static uint32_t app_threadx_thermal_ai_collect_candidates(float output_scale,
                                                          int16_t output_zero_point,
                                                          uint8_t channel_planar_layout,
                                                          uint32_t *raw_candidate_count_ptr,
                                                          uint16_t *max_score_permille_ptr,
                                                          uint16_t *max_objectness_permille_ptr)
{
  float stride_x = (float)CFG_THERMAL_AI_INPUT_WIDTH / (float)CFG_THERMAL_AI_GRID_WIDTH;
  float stride_y = (float)CFG_THERMAL_AI_INPUT_HEIGHT / (float)CFG_THERMAL_AI_GRID_HEIGHT;
  uint32_t grid_y;
  uint32_t grid_x;
  uint32_t raw_candidate_count = 0U;
  uint32_t candidate_count = 0U;
  uint16_t max_score_permille = 0U;
  uint16_t max_objectness_permille = 0U;

  for (grid_y = 0U; grid_y < CFG_THERMAL_AI_GRID_HEIGHT; grid_y++)
  {
    for (grid_x = 0U; grid_x < CFG_THERMAL_AI_GRID_WIDTH; grid_x++)
    {
      float logits[CFG_THERMAL_AI_OUTPUT_CHANNELS];
      float objectness_score;
      float class_scores[CFG_THERMAL_AI_DETECTION_CLASS_COUNT];
      uint32_t class_index = 0U;
      uint8_t class_id;
      float class_score;
      float detection_score;
      float score_threshold;
      float offset_x;
      float offset_y;
      float width_norm;
      float height_norm;
      float center_x;
      float center_y;
      float box_width;
      float box_height;
      float x_min;
      float y_min;
      float x_max;
      float y_max;
      app_threadx_ai_candidate_t *candidate_ptr;
      uint32_t channel_index;

      for (channel_index = 0U; channel_index < CFG_THERMAL_AI_OUTPUT_CHANNELS; channel_index++)
      {
        logits[channel_index] =
            ((float)((int32_t)app_threadx_thermal_ai_read_output_q7(grid_y,
                                                                    grid_x,
                                                                    channel_index,
                                                                    channel_planar_layout) -
                     (int32_t)output_zero_point)) *
            output_scale;
      }

      objectness_score = app_threadx_thermal_ai_sigmoid(logits[0]);
      if ((uint16_t)(objectness_score * 1000.0f + 0.5f) > max_objectness_permille)
      {
        max_objectness_permille = (uint16_t)(objectness_score * 1000.0f + 0.5f);
      }
      if (objectness_score < CFG_THERMAL_AI_OBJECTNESS_THRESHOLD)
      {
        continue;
      }

      for (channel_index = 0U; channel_index < CFG_THERMAL_AI_DETECTION_CLASS_COUNT; channel_index++)
      {
        class_scores[channel_index] = app_threadx_thermal_ai_sigmoid(logits[5U + channel_index]);
        if (class_scores[channel_index] > class_scores[class_index])
        {
          class_index = channel_index;
        }
      }

      class_score = class_scores[class_index];
      detection_score = objectness_score * class_score;
      score_threshold = app_threadx_thermal_ai_score_threshold_for_class(class_index);
      if ((uint16_t)(detection_score * 1000.0f + 0.5f) > max_score_permille)
      {
        max_score_permille = (uint16_t)(detection_score * 1000.0f + 0.5f);
      }
      if ((class_score < CFG_THERMAL_AI_CLASS_THRESHOLD) || (detection_score < score_threshold))
      {
        continue;
      }

      raw_candidate_count++;

      offset_x = app_threadx_thermal_ai_sigmoid(logits[1]);
      offset_y = app_threadx_thermal_ai_sigmoid(logits[2]);
      width_norm = app_threadx_thermal_ai_sigmoid(logits[3]);
      height_norm = app_threadx_thermal_ai_sigmoid(logits[4]);
      center_x = ((float)grid_x + offset_x) * stride_x;
      center_y = ((float)grid_y + offset_y) * stride_y;
      box_width = width_norm * (float)CFG_THERMAL_AI_INPUT_WIDTH;
      box_height = height_norm * (float)CFG_THERMAL_AI_INPUT_HEIGHT;
      if (box_width < 1.0f)
      {
        box_width = 1.0f;
      }
      if (box_height < 1.0f)
      {
        box_height = 1.0f;
      }

      x_min = center_x - (box_width * 0.5f);
      y_min = center_y - (box_height * 0.5f);
      x_max = center_x + (box_width * 0.5f);
      y_max = center_y + (box_height * 0.5f);
      if (x_min < 0.0f)
      {
        x_min = 0.0f;
      }
      if (y_min < 0.0f)
      {
        y_min = 0.0f;
      }
      if (x_max > (float)CFG_THERMAL_AI_INPUT_WIDTH)
      {
        x_max = (float)CFG_THERMAL_AI_INPUT_WIDTH;
      }
      if (y_max > (float)CFG_THERMAL_AI_INPUT_HEIGHT)
      {
        y_max = (float)CFG_THERMAL_AI_INPUT_HEIGHT;
      }

      class_id = app_threadx_thermal_ai_class_index_to_runtime_id(class_index);
      if (app_threadx_thermal_ai_box_passes_postfilter(class_id, x_min, y_min, x_max, y_max) == 0U)
      {
        continue;
      }

      if (candidate_count >= CFG_THERMAL_AI_MAX_CANDIDATES)
      {
        continue;
      }

      candidate_ptr = &g_thermal_ai_candidates[candidate_count++];
      candidate_ptr->class_id = class_id;
      candidate_ptr->score = detection_score;
      candidate_ptr->confidence_permille = (uint16_t)(detection_score * 1000.0f + 0.5f);
      candidate_ptr->x_min = x_min;
      candidate_ptr->y_min = y_min;
      candidate_ptr->x_max = x_max;
      candidate_ptr->y_max = y_max;
    }
  }

  if (raw_candidate_count_ptr != NULL)
  {
    *raw_candidate_count_ptr = raw_candidate_count;
  }
  if (max_score_permille_ptr != NULL)
  {
    *max_score_permille_ptr = max_score_permille;
  }
  if (max_objectness_permille_ptr != NULL)
  {
    *max_objectness_permille_ptr = max_objectness_permille;
  }

  return candidate_count;
}

/**
  * @brief  Compute the IoU between two decoded AI candidates.
  * @param  lhs_ptr First candidate.
  * @param  rhs_ptr Second candidate.
  * @retval float Intersection-over-union ratio.
  */
static float app_threadx_thermal_ai_candidate_iou(const app_threadx_ai_candidate_t *lhs_ptr,
                                                  const app_threadx_ai_candidate_t *rhs_ptr)
{
  float inter_x_min;
  float inter_y_min;
  float inter_x_max;
  float inter_y_max;
  float inter_w;
  float inter_h;
  float inter_area;
  float lhs_area;
  float rhs_area;
  float union_area;

  if ((lhs_ptr == NULL) || (rhs_ptr == NULL))
  {
    return 0.0f;
  }

  inter_x_min = (lhs_ptr->x_min > rhs_ptr->x_min) ? lhs_ptr->x_min : rhs_ptr->x_min;
  inter_y_min = (lhs_ptr->y_min > rhs_ptr->y_min) ? lhs_ptr->y_min : rhs_ptr->y_min;
  inter_x_max = (lhs_ptr->x_max < rhs_ptr->x_max) ? lhs_ptr->x_max : rhs_ptr->x_max;
  inter_y_max = (lhs_ptr->y_max < rhs_ptr->y_max) ? lhs_ptr->y_max : rhs_ptr->y_max;
  inter_w = inter_x_max - inter_x_min;
  inter_h = inter_y_max - inter_y_min;
  if ((inter_w <= 0.0f) || (inter_h <= 0.0f))
  {
    return 0.0f;
  }

  inter_area = inter_w * inter_h;
  lhs_area = (lhs_ptr->x_max - lhs_ptr->x_min) * (lhs_ptr->y_max - lhs_ptr->y_min);
  rhs_area = (rhs_ptr->x_max - rhs_ptr->x_min) * (rhs_ptr->y_max - rhs_ptr->y_min);
  union_area = lhs_area + rhs_area - inter_area;
  if (union_area <= 0.0f)
  {
    return 0.0f;
  }

  return inter_area / union_area;
}

/**
  * @brief  Decode the current raw network output buffer into project detections.
  * @param  result_ptr Output runtime result object.
  * @retval None
  */
static void app_threadx_thermal_ai_decode_output(thermal_ai_result_t *result_ptr)
{
  float output_scale;
  int16_t output_zero_point;
  uint32_t grid_y;
  uint32_t grid_x;
  uint32_t candidate_count = 0U;
  uint32_t kept_count = 0U;
  uint32_t raw_candidate_count = 0U;
#if (CFG_THERMAL_AI_ALLOW_PLANAR_OUTPUT_FALLBACK != 0U)
  uint32_t fallback_raw_candidate_count = 0U;
  uint32_t fallback_candidate_count = 0U;
#endif
  uint32_t class_loop_index;
  uint8_t candidate_used[CFG_THERMAL_AI_MAX_CANDIDATES];
  uint16_t max_score_permille = 0U;
#if (CFG_THERMAL_AI_ALLOW_PLANAR_OUTPUT_FALLBACK != 0U)
  uint16_t fallback_max_score_permille = 0U;
#endif
  uint16_t max_objectness_permille = 0U;
#if (CFG_THERMAL_AI_ALLOW_PLANAR_OUTPUT_FALLBACK != 0U)
  uint16_t fallback_max_objectness_permille = 0U;
#endif
  uint8_t channel_planar_layout = 0U;
  static const uint8_t class_order[] = {
    (uint8_t)THERMAL_AI_CLASS_CIRCUIT_BOARD_ABNORMAL_HOTSPOT
  };

  if ((result_ptr == NULL) || (g_thermal_ai_output_buffer == NULL))
  {
    return;
  }

  output_scale = (g_thermal_ai_output_info[0].scale != NULL) ? g_thermal_ai_output_info[0].scale[0] : 1.0f;
  output_zero_point = (g_thermal_ai_output_info[0].offset != NULL) ? g_thermal_ai_output_info[0].offset[0] : 0;
  channel_planar_layout = app_threadx_thermal_ai_output_is_planar();
  candidate_count = app_threadx_thermal_ai_collect_candidates(output_scale,
                                                              output_zero_point,
                                                              channel_planar_layout,
                                                              &raw_candidate_count,
                                                              &max_score_permille,
                                                              &max_objectness_permille);
  g_thermal_ai_diag_output_layout = channel_planar_layout;
#if (CFG_THERMAL_AI_ALLOW_PLANAR_OUTPUT_FALLBACK != 0U)
  if (candidate_count == 0U)
  {
    fallback_candidate_count = app_threadx_thermal_ai_collect_candidates(output_scale,
                                                                         output_zero_point,
                                                                         1U,
                                                                         &fallback_raw_candidate_count,
                                                                         &fallback_max_score_permille,
                                                                         &fallback_max_objectness_permille);
    if ((fallback_candidate_count > 0U) ||
        (fallback_raw_candidate_count > raw_candidate_count) ||
        (fallback_max_score_permille > max_score_permille) ||
        (fallback_max_objectness_permille > max_objectness_permille))
    {
      candidate_count = fallback_candidate_count;
      raw_candidate_count = fallback_raw_candidate_count;
      max_score_permille = fallback_max_score_permille;
      max_objectness_permille = fallback_max_objectness_permille;
      g_thermal_ai_diag_output_layout = 1U;
    }
    else
    {
      candidate_count = app_threadx_thermal_ai_collect_candidates(output_scale,
                                                                  output_zero_point,
                                                                  0U,
                                                                  &raw_candidate_count,
                                                                  &max_score_permille,
                                                                  &max_objectness_permille);
    }
  }
#endif

  (void)memset(candidate_used, 0, sizeof(candidate_used));
  for (class_loop_index = 0U; class_loop_index < (sizeof(class_order) / sizeof(class_order[0])); class_loop_index++)
  {
    uint8_t target_class_id = class_order[class_loop_index];

    for (;;)
    {
      uint32_t best_index = CFG_THERMAL_AI_MAX_CANDIDATES;
      float best_score = -1.0f;
      uint32_t candidate_index;

      for (candidate_index = 0U; candidate_index < candidate_count; candidate_index++)
      {
        if ((candidate_used[candidate_index] != 0U) ||
            (g_thermal_ai_candidates[candidate_index].class_id != target_class_id))
        {
          continue;
        }
        if (g_thermal_ai_candidates[candidate_index].score > best_score)
        {
          best_score = g_thermal_ai_candidates[candidate_index].score;
          best_index = candidate_index;
        }
      }

      if (best_index >= candidate_count)
      {
        break;
      }

      g_thermal_ai_kept_candidates[kept_count++] = g_thermal_ai_candidates[best_index];
      candidate_used[best_index] = 1U;
      for (candidate_index = 0U; candidate_index < candidate_count; candidate_index++)
      {
        if ((candidate_used[candidate_index] == 0U) &&
            (g_thermal_ai_candidates[candidate_index].class_id == target_class_id) &&
            (app_threadx_thermal_ai_candidate_iou(&g_thermal_ai_candidates[best_index],
                                                  &g_thermal_ai_candidates[candidate_index]) >= CFG_THERMAL_AI_NMS_IOU_THRESHOLD))
        {
          candidate_used[candidate_index] = 1U;
        }
      }
    }
  }

  for (grid_y = 0U; grid_y < kept_count; grid_y++)
  {
    for (grid_x = grid_y + 1U; grid_x < kept_count; grid_x++)
    {
      if (g_thermal_ai_kept_candidates[grid_x].score > g_thermal_ai_kept_candidates[grid_y].score)
      {
        app_threadx_ai_candidate_t temp = g_thermal_ai_kept_candidates[grid_y];
        g_thermal_ai_kept_candidates[grid_y] = g_thermal_ai_kept_candidates[grid_x];
        g_thermal_ai_kept_candidates[grid_x] = temp;
      }
    }
  }

  if (kept_count > CFG_THERMAL_AI_MAX_DETECTIONS)
  {
    kept_count = CFG_THERMAL_AI_MAX_DETECTIONS;
  }

  result_ptr->detection_count = (uint8_t)kept_count;
  for (grid_y = 0U; grid_y < CFG_THERMAL_AI_RUNTIME_MAX_DETECTIONS; grid_y++)
  {
    result_ptr->detections[grid_y].valid = 0U;
  }

  for (grid_y = 0U; grid_y < kept_count; grid_y++)
  {
    const app_threadx_ai_candidate_t *candidate_ptr = &g_thermal_ai_kept_candidates[grid_y];
    thermal_ai_detection_t *detection_ptr = &result_ptr->detections[grid_y];

    detection_ptr->valid = 1U;
    detection_ptr->class_id = candidate_ptr->class_id;
    detection_ptr->confidence_permille = candidate_ptr->confidence_permille;
    detection_ptr->bbox.x_min = (uint16_t)(candidate_ptr->x_min + 0.5f);
    detection_ptr->bbox.y_min = (uint16_t)(candidate_ptr->y_min + 0.5f);
    detection_ptr->bbox.x_max = (uint16_t)(candidate_ptr->x_max + 0.5f);
    detection_ptr->bbox.y_max = (uint16_t)(candidate_ptr->y_max + 0.5f);
  }

  g_thermal_ai_diag_raw_candidate_count = (uint16_t)raw_candidate_count;
  g_thermal_ai_diag_postfilter_candidate_count = (uint16_t)candidate_count;
  g_thermal_ai_diag_max_score_permille = max_score_permille;
  g_thermal_ai_diag_max_objectness_permille = max_objectness_permille;
}

#if (CFG_THERMAL_AI_HOTSPOT_FALLBACK_ENABLE != 0U)
/**
  * @brief  Report whether a result already contains an abnormal-hotspot detection.
  * @param  result_ptr Runtime AI result pointer.
  * @retval uint8_t 1 when a hotspot detection is present, otherwise 0.
  */
static uint8_t app_threadx_thermal_ai_result_has_hotspot(const thermal_ai_result_t *result_ptr)
{
  uint32_t detection_index;

  if (result_ptr == NULL)
  {
    return 0U;
  }

  for (detection_index = 0U; detection_index < result_ptr->detection_count; detection_index++)
  {
    if ((result_ptr->detections[detection_index].valid != 0U) &&
        (result_ptr->detections[detection_index].class_id == (uint8_t)THERMAL_AI_CLASS_CIRCUIT_BOARD_ABNORMAL_HOTSPOT))
    {
      return 1U;
    }
  }

  return 0U;
}

/**
  * @brief  Collect one hotspot candidate box from the current preview image and confirm it with temp14.
  * @param  background_temp14 Median/background temperature in kelvin*16.
  * @param  hot_pixel_count_ptr Output count of confirmed hot pixels.
  * @param  max_x_ptr Output hottest candidate X coordinate.
  * @param  max_y_ptr Output hottest candidate Y coordinate.
  * @param  x_min_ptr Output candidate box minimum X.
  * @param  y_min_ptr Output candidate box minimum Y.
  * @param  x_max_ptr Output candidate box maximum X (exclusive).
  * @param  y_max_ptr Output candidate box maximum Y (exclusive).
  * @retval uint8_t 1 when a valid preview-guided candidate exists, otherwise 0.
  */
static uint8_t app_threadx_thermal_ai_collect_preview_hotspot_box(uint16_t background_temp14,
                                                                  uint32_t *hot_pixel_count_ptr,
                                                                  uint16_t *max_x_ptr,
                                                                  uint16_t *max_y_ptr,
                                                                  uint16_t *x_min_ptr,
                                                                  uint16_t *y_min_ptr,
                                                                  uint16_t *x_max_ptr,
                                                                  uint16_t *y_max_ptr,
                                                                  uint8_t *background_score_ptr,
                                                                  uint8_t *peak_score_ptr)
{
  const uint16_t *preview_rgb565_frame;
  uint16_t preview_width;
  uint16_t preview_height;
  uint16_t score_histogram[CFG_THERMAL_AI_PREVIEW_SCORE_BINS];
  uint8_t pseudo_mode;
  uint8_t score_background;
  uint8_t score_threshold;
  uint16_t score_threshold_u16;
  uint16_t confirm_delta_temp14;
  uint32_t valid_score_count = 0U;
  uint32_t hot_pixel_count = 0U;
  uint32_t pixel_index = 0U;
  uint16_t max_temp14 = 0U;
  uint8_t max_temp14_valid = 0U;
  uint8_t max_preview_score = 0U;
  uint16_t max_x = 0U;
  uint16_t max_y = 0U;
  uint16_t x_min = CFG_THERMAL_AI_INPUT_WIDTH;
  uint16_t y_min = CFG_THERMAL_AI_INPUT_HEIGHT;
  uint16_t x_max = 0U;
  uint16_t y_max = 0U;
  uint32_t y_index;
  uint32_t x_index;

  if ((hot_pixel_count_ptr == NULL) || (max_x_ptr == NULL) || (max_y_ptr == NULL) ||
      (x_min_ptr == NULL) || (y_min_ptr == NULL) || (x_max_ptr == NULL) || (y_max_ptr == NULL) ||
      (background_score_ptr == NULL) || (peak_score_ptr == NULL))
  {
    return 0U;
  }

  preview_rgb565_frame = tiny1c_thermal_app_get_rgb565_frame();
  preview_width = tiny1c_thermal_app_get_preview_width();
  preview_height = tiny1c_thermal_app_get_preview_height();
  if ((preview_rgb565_frame == NULL) || (preview_width == 0U) || (preview_height == 0U))
  {
    return 0U;
  }

  (void)memset(score_histogram, 0, sizeof(score_histogram));
  pseudo_mode = app_threadx_thermal_ai_query_preview_pseudo_mode();
  confirm_delta_temp14 = app_threadx_thermal_ai_preview_confirm_delta_for_mode(pseudo_mode);
  if ((confirm_delta_temp14 != 0U) &&
      ((background_temp14 < CFG_THERMAL_AI_VALID_TEMP14_MIN) ||
       (background_temp14 > CFG_THERMAL_AI_PREVIEW_CONFIRM_VALID_MAX_TEMP14)))
  {
    return 0U;
  }

  for (y_index = 0U; y_index < CFG_THERMAL_AI_INPUT_HEIGHT; y_index++)
  {
    uint32_t py0 = (y_index * preview_height) / CFG_THERMAL_AI_INPUT_HEIGHT;

    for (x_index = 0U; x_index < CFG_THERMAL_AI_INPUT_WIDTH; x_index++)
    {
      uint32_t px0 = (x_index * preview_width) / CFG_THERMAL_AI_INPUT_WIDTH;
      uint16_t preview_rgb565;
      uint8_t preview_score;

      preview_rgb565 = preview_rgb565_frame[py0 * preview_width + px0];
      preview_score = app_threadx_thermal_ai_preview_hotness_score(preview_rgb565, pseudo_mode);
#if (CFG_THERMAL_AI_TOPRIGHT_BIAS_COMP_ENABLE != 0U)
      if ((app_threadx_thermal_ai_is_topright_bias_region((uint16_t)x_index, (uint16_t)y_index) != 0U) &&
          (preview_score > CFG_THERMAL_AI_TOPRIGHT_BIAS_SCORE))
      {
        preview_score = (uint8_t)(preview_score - CFG_THERMAL_AI_TOPRIGHT_BIAS_SCORE);
      }
#endif
      score_histogram[preview_score]++;
      valid_score_count++;
    }
  }

  if (valid_score_count == 0U)
  {
    return 0U;
  }

  score_background = app_threadx_thermal_ai_percentile_from_u8_histogram(score_histogram,
                                                                          valid_score_count,
                                                                          50U,
                                                                          100U);
  score_threshold_u16 = (uint16_t)score_background +
                        (uint16_t)app_threadx_thermal_ai_preview_hot_score_delta_for_mode(pseudo_mode);
  score_threshold = (score_threshold_u16 > 255U) ? 255U : (uint8_t)score_threshold_u16;
  if (score_threshold < app_threadx_thermal_ai_preview_min_hot_score_for_mode(pseudo_mode))
  {
    score_threshold = app_threadx_thermal_ai_preview_min_hot_score_for_mode(pseudo_mode);
  }

  pixel_index = 0U;
  for (y_index = 0U; y_index < CFG_THERMAL_AI_INPUT_HEIGHT; y_index++)
  {
    uint32_t py0 = (y_index * preview_height) / CFG_THERMAL_AI_INPUT_HEIGHT;

    for (x_index = 0U; x_index < CFG_THERMAL_AI_INPUT_WIDTH; x_index++, pixel_index++)
    {
      uint32_t px0 = (x_index * preview_width) / CFG_THERMAL_AI_INPUT_WIDTH;
      uint16_t preview_rgb565;
      uint8_t preview_score;
      uint16_t temp14_value = g_thermal_ai_oriented_temp14[pixel_index];
      uint8_t temp14_valid;

      preview_rgb565 = preview_rgb565_frame[py0 * preview_width + px0];
      preview_score = app_threadx_thermal_ai_preview_hotness_score(preview_rgb565, pseudo_mode);
#if (CFG_THERMAL_AI_TOPRIGHT_BIAS_COMP_ENABLE != 0U)
      if ((app_threadx_thermal_ai_is_topright_bias_region((uint16_t)x_index, (uint16_t)y_index) != 0U) &&
          (preview_score > CFG_THERMAL_AI_TOPRIGHT_BIAS_SCORE))
      {
        preview_score = (uint8_t)(preview_score - CFG_THERMAL_AI_TOPRIGHT_BIAS_SCORE);
      }
#endif

      if (preview_score < score_threshold)
      {
        continue;
      }

      if (preview_score > max_preview_score)
      {
        max_preview_score = preview_score;
        max_x = (uint16_t)x_index;
        max_y = (uint16_t)y_index;
      }

      temp14_valid = ((temp14_value >= CFG_THERMAL_AI_VALID_TEMP14_MIN) &&
                      (temp14_value <= CFG_THERMAL_AI_VALID_TEMP14_MAX))
                        ? 1U
                        : 0U;
      if ((confirm_delta_temp14 != 0U) &&
          (((uint32_t)temp14_value < ((uint32_t)background_temp14 + confirm_delta_temp14)) ||
           (temp14_value > CFG_THERMAL_AI_PREVIEW_CONFIRM_VALID_MAX_TEMP14)))
      {
        continue;
      }
      hot_pixel_count++;
      if (temp14_valid != 0U)
      {
        if ((max_temp14_valid == 0U) || (temp14_value > max_temp14))
        {
          max_temp14_valid = 1U;
          max_temp14 = temp14_value;
          max_x = (uint16_t)x_index;
          max_y = (uint16_t)y_index;
        }
      }
      if (x_index < x_min)
      {
        x_min = (uint16_t)x_index;
      }
      if (y_index < y_min)
      {
        y_min = (uint16_t)y_index;
      }
      if (x_index > x_max)
      {
        x_max = (uint16_t)x_index;
      }
      if (y_index > y_max)
      {
        y_max = (uint16_t)y_index;
      }
    }
  }

  if (hot_pixel_count < app_threadx_thermal_ai_preview_min_hot_pixels_for_mode(pseudo_mode))
  {
    return 0U;
  }

  *hot_pixel_count_ptr = hot_pixel_count;
  *max_x_ptr = max_x;
  *max_y_ptr = max_y;
  *x_min_ptr = x_min;
  *y_min_ptr = y_min;
  *x_max_ptr = (uint16_t)(x_max + 1U);
  *y_max_ptr = (uint16_t)(y_max + 1U);
  *background_score_ptr = score_background;
  *peak_score_ptr = max_preview_score;
  return 1U;
}

/**
  * @brief  Add a conservative temperature-derived hotspot box when the model only reports normal.
  * @param  result_ptr Runtime AI result pointer to update.
  * @param  background_temp14 Median/background temperature in kelvin*16.
  * @retval None
  */
static void app_threadx_thermal_ai_append_hotspot_fallback(thermal_ai_result_t *result_ptr,
                                                           uint16_t background_temp14)
{
  uint32_t pixel_index;
  uint32_t hot_pixel_count = 0U;
  uint32_t slot_index;
  uint8_t preview_guided = 0U;
  uint16_t threshold_temp14;
  uint16_t max_temp14 = 0U;
  uint16_t max_x = 0U;
  uint16_t max_y = 0U;
  uint16_t x_min = CFG_THERMAL_AI_INPUT_WIDTH;
  uint16_t y_min = CFG_THERMAL_AI_INPUT_HEIGHT;
  uint16_t x_max = 0U;
  uint16_t y_max = 0U;
  uint16_t confidence_permille;
  uint8_t background_score = 0U;
  uint8_t peak_score = 0U;
  uint32_t box_width;
  uint32_t box_height;

  if ((result_ptr == NULL) ||
      (app_threadx_thermal_ai_result_has_hotspot(result_ptr) != 0U))
  {
    return;
  }

  if ((CFG_THERMAL_AI_TEMP14_HOTSPOT_ONLY != 0U) ||
      (app_threadx_thermal_ai_collect_preview_hotspot_box(background_temp14,
                                                          &hot_pixel_count,
                                                          &max_x,
                                                          &max_y,
                                                          &x_min,
                                                          &y_min,
                                                          &x_max,
                                                          &y_max,
                                                          &background_score,
                                                          &peak_score) == 0U))
  {
    if ((background_temp14 < CFG_THERMAL_AI_VALID_TEMP14_MIN) ||
        (background_temp14 > CFG_THERMAL_AI_PREVIEW_CONFIRM_VALID_MAX_TEMP14))
    {
      return;
    }

    if (((uint32_t)background_temp14 + CFG_THERMAL_AI_HOTSPOT_FALLBACK_DELTA_TEMP14) >
        CFG_THERMAL_AI_PREVIEW_CONFIRM_VALID_MAX_TEMP14)
    {
      return;
    }
    threshold_temp14 = (uint16_t)(background_temp14 + CFG_THERMAL_AI_HOTSPOT_FALLBACK_DELTA_TEMP14);

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

        pixel_threshold_temp14 = (topright_threshold_temp14 <=
                                  CFG_THERMAL_AI_PREVIEW_CONFIRM_VALID_MAX_TEMP14)
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

      {
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
    }

    if (hot_pixel_count != 0U)
    {
      x_max = (uint16_t)(x_max + 1U);
      y_max = (uint16_t)(y_max + 1U);
    }
  }
  else
  {
    preview_guided = 1U;
  }

  if (hot_pixel_count < CFG_THERMAL_AI_HOTSPOT_FALLBACK_MIN_PIXELS)
  {
    return;
  }

  if (preview_guided != 0U)
  {
    if (x_min > 1U)
    {
      x_min = (uint16_t)(x_min - 1U);
    }
    if (y_min > 1U)
    {
      y_min = (uint16_t)(y_min - 1U);
    }
    if (x_max + 1U < CFG_THERMAL_AI_INPUT_WIDTH)
    {
      x_max = (uint16_t)(x_max + 1U);
    }
    if (y_max + 1U < CFG_THERMAL_AI_INPUT_HEIGHT)
    {
      y_max = (uint16_t)(y_max + 1U);
    }
  }

  box_width = (uint32_t)x_max - (uint32_t)x_min;
  box_height = (uint32_t)y_max - (uint32_t)y_min;

  if (preview_guided == 0U)
  {
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

  if ((preview_guided == 0U) && (max_temp14 > background_temp14))
  {
    uint32_t temp_delta_temp14 = (uint32_t)max_temp14 - (uint32_t)background_temp14;

    /* Map a 0..20 degC rise into the same 0..255 contrast domain used by
     * RGB hotspot confidence. Values above 20 degC saturate naturally. */
    background_score = 0U;
    peak_score = (temp_delta_temp14 >= 320U)
                   ? 255U
                   : (uint8_t)((temp_delta_temp14 * 255U) / 320U);
  }

  confidence_permille = app_threadx_thermal_ai_calc_hotspot_fallback_confidence(preview_guided,
                                                                                 hot_pixel_count,
                                                                                 (uint32_t)x_max - (uint32_t)x_min,
                                                                                 (uint32_t)y_max - (uint32_t)y_min,
                                                                                 background_score,
                                                                                 peak_score);

  slot_index = result_ptr->detection_count;
  if (slot_index >= CFG_THERMAL_AI_MAX_DETECTIONS)
  {
    for (slot_index = 0U; slot_index < CFG_THERMAL_AI_MAX_DETECTIONS; slot_index++)
    {
      if (result_ptr->detections[slot_index].class_id == (uint8_t)THERMAL_AI_CLASS_CIRCUIT_BOARD_NORMAL)
      {
        break;
      }
    }
    if (slot_index >= CFG_THERMAL_AI_MAX_DETECTIONS)
    {
      return;
    }
  }
  else
  {
    result_ptr->detection_count++;
  }

  result_ptr->detections[slot_index].valid = 1U;
  result_ptr->detections[slot_index].class_id = (uint8_t)THERMAL_AI_CLASS_CIRCUIT_BOARD_ABNORMAL_HOTSPOT;
  result_ptr->detections[slot_index].confidence_permille = confidence_permille;
  result_ptr->detections[slot_index].bbox.x_min = x_min;
  result_ptr->detections[slot_index].bbox.y_min = y_min;
  result_ptr->detections[slot_index].bbox.x_max = x_max;
  result_ptr->detections[slot_index].bbox.y_max = y_max;
}
#endif

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
  * @brief  Track the last epoch-block index reached by one thermal-network execution.
  * @param  ctype Runtime callback type.
  * @param  nn_instance Network instance pointer.
  * @param  epoch_block Epoch-block pointer associated with this callback.
  * @retval None
  */
static void app_threadx_thermal_ai_epoch_callback(LL_ATON_RT_Callbacktype_t ctype,
                                                  const NN_Instance_TypeDef *nn_instance,
                                                  const EpochBlock_ItemTypeDef *epoch_block)
{
  const EpochBlock_ItemTypeDef *first_epoch_block;
  const LL_Buffer_InfoTypeDef *self_test_buffer_info_ptr;
  uint32_t epoch_index;

  if ((ctype != LL_ATON_RT_Callbacktype_PRE_START) &&
      (ctype != LL_ATON_RT_Callbacktype_POST_END))
  {
    return;
  }

  if ((nn_instance == NULL) || (epoch_block == NULL))
  {
    return;
  }

  first_epoch_block = nn_instance->exec_state.first_epoch_block;
  if ((first_epoch_block == NULL) || (epoch_block < first_epoch_block))
  {
    return;
  }

  epoch_index = (uint32_t)(epoch_block - first_epoch_block);
  if (epoch_index > 0xFFU)
  {
    epoch_index = 0xFFU;
  }

  g_thermal_ai_epoch_callback_last_index = (uint8_t)epoch_index;

  if ((ctype == LL_ATON_RT_Callbacktype_POST_END) &&
      (g_thermal_ai_self_test_done == 0U) &&
      (epoch_index >= 2U) &&
      (epoch_index <= 5U))
  {
    const char *buffer_name_ptr = NULL;
    uint8_t *byte0_dest_ptr = NULL;
    uint8_t valid_mask_bit = 0U;

    switch (epoch_index)
    {
      case 2U:
        buffer_name_ptr = "Conv2D_3_zero_off_out_1";
        byte0_dest_ptr = (uint8_t *)&g_thermal_ai_self_test_pipeline_byte0_conv3;
        valid_mask_bit = 0x01U;
        break;

      case 3U:
        buffer_name_ptr = "Conv2D_9_zero_off_out_10";
        byte0_dest_ptr = (uint8_t *)&g_thermal_ai_self_test_pipeline_byte0_conv9;
        valid_mask_bit = 0x02U;
        break;

      case 4U:
        buffer_name_ptr = "Conv2D_15_zero_off_out_19";
        byte0_dest_ptr = (uint8_t *)&g_thermal_ai_self_test_pipeline_byte0_conv15;
        valid_mask_bit = 0x04U;
        break;

      case 5U:
        buffer_name_ptr = "Conv2D_21_zero_off_out_28";
        byte0_dest_ptr = (uint8_t *)&g_thermal_ai_self_test_pipeline_byte0_conv21;
        valid_mask_bit = 0x08U;
        break;

      default:
        break;
    }

    if ((buffer_name_ptr != NULL) && (byte0_dest_ptr != NULL))
    {
      self_test_buffer_info_ptr = app_threadx_thermal_ai_find_buffer_by_name(
          LL_ATON_Internal_Buffers_Info_thermal(),
          buffer_name_ptr);
      if (app_threadx_thermal_ai_capture_buffer_byte0(self_test_buffer_info_ptr, byte0_dest_ptr) != 0U)
      {
        g_thermal_ai_self_test_pipeline_valid_mask |= valid_mask_bit;
      }
    }
  }

  if ((ctype == LL_ATON_RT_Callbacktype_POST_END) &&
      (g_thermal_ai_self_test_done == 0U) &&
      (epoch_index == 5U))
  {
    self_test_buffer_info_ptr = app_threadx_thermal_ai_find_buffer_by_name(
        LL_ATON_Internal_Buffers_Info_thermal(),
        "Conv2D_21_zero_off_out_28");
    g_thermal_ai_self_test_mid_valid = app_threadx_thermal_ai_capture_buffer_bytes(
        self_test_buffer_info_ptr,
        (uint8_t *)&g_thermal_ai_self_test_mid_byte0,
        (uint8_t *)&g_thermal_ai_self_test_mid_byte1,
        (uint8_t *)&g_thermal_ai_self_test_mid_byte2,
        (uint8_t *)&g_thermal_ai_self_test_mid_byte3);
  }
}

/**
  * @brief  Run one already-initialized thermal network until the inference finishes.
  * @retval uint8_t Last observed epoch-block index after the runtime finished.
  */
static uint8_t app_threadx_thermal_ai_run_initialized_network(void)
{
  LL_ATON_RT_RetValues_t ll_aton_rt_ret;

  if (app_threadx_thermal_ai_prepare_runtime() == 0U)
  {
    return 0U;
  }

  if (g_thermal_ai_ll_network_needs_reset != 0U)
  {
    LL_ATON_RT_Reset_Network(&NN_Instance_thermal);
  }

  g_thermal_ai_epoch_callback_last_index = 0U;
  LL_ATON_RT_ClearIrqErrorState();

  do
  {
    ll_aton_rt_ret = LL_ATON_RT_RunEpochBlock(&NN_Instance_thermal);
    if (ll_aton_rt_ret == LL_ATON_RT_WFE)
    {
      LL_ATON_OSAL_WFE();
    }
  } while (ll_aton_rt_ret != LL_ATON_RT_DONE);

  g_thermal_ai_ll_network_needs_reset = 1U;
  return g_thermal_ai_epoch_callback_last_index;
}

/**
  * @brief  Execute one complete thermal AI inference on the latest temp14 frame.
  * @param  temp14_frame Pointer to the latest raw temp14 frame.
  * @param  frame_counter Current frame counter.
  * @retval None
  */
static void app_threadx_thermal_ai_run_inference(const uint16_t *temp14_frame, uint32_t frame_counter)
{
  thermal_ai_result_t result;
  uint16_t background_temp14 = 0U;
  uint16_t max_temp14 = 0U;
  ULONG tick_start;
  uint8_t last_epoch_index = 0U;

  if ((CFG_THERMAL_AI_ENABLE == 0U) || (app_thermal_ai_is_enabled() == 0U))
  {
    return;
  }

#if (CFG_THERMAL_AI_PREVIEW_HOTSPOT_ONLY != 0U)
  app_threadx_thermal_ai_run_preview_hotspot(temp14_frame, frame_counter);
  return;
#endif

  g_thermal_ai_diag_run_count++;
  app_threadx_thermal_ai_set_diag_stage(1U, 1U);

  if (g_thermal_ai_iac_fault_latched != 0U)
  {
    return;
  }

  if ((temp14_frame == NULL) || (app_threadx_thermal_ai_prepare_io() == 0U))
  {
    return;
  }

  g_thermal_ai_diag_weights_ok = app_threadx_thermal_ai_probe_weight_signature();
  g_thermal_ai_diag_output_byte0 = 0U;
  g_thermal_ai_diag_raw_candidate_count = 0U;
  g_thermal_ai_diag_postfilter_candidate_count = 0U;
  g_thermal_ai_diag_max_score_permille = 0U;
  g_thermal_ai_diag_max_objectness_permille = 0U;
  g_thermal_ai_diag_output_layout = 0U;
  g_thermal_ai_diag_epoch_index = 0U;
  g_thermal_ai_diag_aton_irq_error_latched = 0U;
  g_thermal_ai_diag_aton_irq_error_irqs = 0U;
  g_thermal_ai_diag_aton_irq_error_aux = 0U;

  app_threadx_thermal_ai_set_diag_stage(13U, 1U);
  if (app_threadx_thermal_ai_prepare_weight_cache() == 0U)
  {
    app_threadx_thermal_ai_set_diag_stage(14U, 0U);
    return;
  }

#if ((CFG_THERMAL_AI_SELF_TEST_ENABLE != 0U) && (CFG_THERMAL_AI_USE_REFERENCE_INPUT == 0U))
  if (g_thermal_ai_self_test_done == 0U)
  {
    g_thermal_ai_self_test_mid_valid = 0U;
    g_thermal_ai_self_test_mid_byte0 = 0U;
    g_thermal_ai_self_test_mid_byte1 = 0U;
    g_thermal_ai_self_test_mid_byte2 = 0U;
    g_thermal_ai_self_test_mid_byte3 = 0U;
    g_thermal_ai_self_test_pipeline_valid_mask = 0U;
    g_thermal_ai_self_test_pipeline_byte0_conv3 = 0U;
    g_thermal_ai_self_test_pipeline_byte0_conv9 = 0U;
    g_thermal_ai_self_test_pipeline_byte0_conv15 = 0U;
    g_thermal_ai_self_test_pipeline_byte0_conv21 = 0U;
    if (app_threadx_thermal_ai_load_reference_input(&background_temp14, &max_temp14) != 0U)
    {
      app_threadx_dcache_clean_by_addr(g_thermal_ai_input_buffer, LL_Buffer_len(&g_thermal_ai_input_info[0]));
      app_threadx_dcache_invalidate_by_addr(g_thermal_ai_input_buffer, LL_Buffer_len(&g_thermal_ai_input_info[0]));
      app_threadx_dcache_invalidate_by_addr(g_thermal_ai_output_buffer, LL_Buffer_len(&g_thermal_ai_output_info[0]));
      g_thermal_ai_self_test_input_xor =
          app_threadx_thermal_ai_calc_xor8(g_thermal_ai_input_buffer, LL_Buffer_len(&g_thermal_ai_input_info[0]));
      last_epoch_index = app_threadx_thermal_ai_run_initialized_network();
      g_thermal_ai_diag_epoch_index = last_epoch_index;
      g_thermal_ai_self_test_aton_irq_error_latched = g_ll_aton_rt_irq_err_latched;
      g_thermal_ai_self_test_aton_irq_error_irqs = (uint16_t)(g_ll_aton_rt_irq_err_irqs_low32 & 0xFFFFU);
      g_thermal_ai_self_test_aton_irq_error_aux =
          (uint16_t)(((g_ll_aton_rt_irq_err_busif_or != 0U) ? g_ll_aton_rt_irq_err_busif_or
                                                            : g_ll_aton_rt_irq_err_epoch_irq) &
                     0xFFFFU);
      app_threadx_dcache_invalidate_by_addr(g_thermal_ai_output_buffer, LL_Buffer_len(&g_thermal_ai_output_info[0]));
      app_threadx_thermal_ai_capture_output_signature(g_thermal_ai_output_buffer,
                                                      LL_Buffer_len(&g_thermal_ai_output_info[0]),
                                                      (uint8_t *)&g_thermal_ai_self_test_output_byte0,
                                                      (uint8_t *)&g_thermal_ai_self_test_output_min,
                                                      (uint8_t *)&g_thermal_ai_self_test_output_max);
      if (LL_Buffer_len(&g_thermal_ai_output_info[0]) >= 4U)
      {
        g_thermal_ai_self_test_output_byte0 = (uint8_t)g_thermal_ai_output_buffer[0];
        g_thermal_ai_self_test_output_byte1 = (uint8_t)g_thermal_ai_output_buffer[1];
        g_thermal_ai_self_test_output_byte2 = (uint8_t)g_thermal_ai_output_buffer[2];
        g_thermal_ai_self_test_output_byte3 = (uint8_t)g_thermal_ai_output_buffer[3];
      }
      (void)memset(&result, 0, sizeof(result));
      if (g_ll_aton_rt_irq_err_latched == 0U)
      {
        app_threadx_thermal_ai_decode_output(&result);
      }
      g_thermal_ai_self_test_output_layout = g_thermal_ai_diag_output_layout;
      g_thermal_ai_self_test_epoch_index = last_epoch_index;
      g_thermal_ai_self_test_pass =
          ((g_ll_aton_rt_irq_err_latched == 0U) && (result.detection_count != 0U)) ? 1U : 0U;
    }
    else
    {
      g_thermal_ai_self_test_pass = 0U;
      g_thermal_ai_self_test_epoch_index = 0U;
      g_thermal_ai_self_test_aton_irq_error_latched = 0U;
      g_thermal_ai_self_test_aton_irq_error_irqs = 0U;
      g_thermal_ai_self_test_aton_irq_error_aux = 0U;
      g_thermal_ai_self_test_mid_valid = 0U;
      g_thermal_ai_self_test_pipeline_valid_mask = 0U;
    }
    g_thermal_ai_self_test_done = 1U;
  }
#endif

#if (CFG_THERMAL_AI_USE_REFERENCE_INPUT != 0U)
  if (app_threadx_thermal_ai_load_reference_input(&background_temp14, &max_temp14) == 0U)
  {
    return;
  }
#else
  app_threadx_thermal_ai_build_corrected_temp14_frame(temp14_frame, g_thermal_ai_corrected_temp14);
  if (app_threadx_thermal_ai_build_input(g_thermal_ai_corrected_temp14, &background_temp14, &max_temp14) == 0U)
  {
    (void)memset(&result, 0, sizeof(result));
    result.frame_counter = frame_counter;
    if (tx_mutex_get(&g_thermal_ai_mutex, TX_WAIT_FOREVER) == TX_SUCCESS)
    {
      if (g_thermal_ai_runtime_enable != 0U)
      {
        thermal_ai_runtime_update(&g_thermal_ai_runtime, &result);
        g_thermal_ai_model_ready = 0U;
      }
      tx_mutex_put(&g_thermal_ai_mutex);
    }
    return;
  }
#endif
  app_threadx_thermal_ai_set_diag_stage(9U, 1U);
  app_threadx_dcache_clean_by_addr(g_thermal_ai_input_buffer, LL_Buffer_len(&g_thermal_ai_input_info[0]));
  app_threadx_dcache_invalidate_by_addr(g_thermal_ai_output_buffer, LL_Buffer_len(&g_thermal_ai_output_info[0]));

  app_threadx_thermal_ai_set_diag_stage(10U, 1U);

  tick_start = HAL_GetTick();
  last_epoch_index = app_threadx_thermal_ai_run_initialized_network();
  g_thermal_ai_last_inference_ms = HAL_GetTick() - tick_start;
  g_thermal_ai_diag_epoch_index = last_epoch_index;
  g_thermal_ai_diag_aton_irq_error_latched = g_ll_aton_rt_irq_err_latched;
  g_thermal_ai_diag_aton_irq_error_irqs = (uint16_t)(g_ll_aton_rt_irq_err_irqs_low32 & 0xFFFFU);
  g_thermal_ai_diag_aton_irq_error_aux =
      (uint16_t)(((g_ll_aton_rt_irq_err_busif_or != 0U) ? g_ll_aton_rt_irq_err_busif_or
                                                        : g_ll_aton_rt_irq_err_epoch_irq) &
                 0xFFFFU);

  app_threadx_thermal_ai_set_diag_stage(11U, 0U);
  app_threadx_dcache_invalidate_by_addr(g_thermal_ai_output_buffer, LL_Buffer_len(&g_thermal_ai_output_info[0]));
  g_thermal_ai_diag_output_byte0 = (uint8_t)g_thermal_ai_output_buffer[0];

  if (g_ll_aton_rt_irq_err_latched != 0U)
  {
    app_threadx_thermal_ai_release_runtime();
    (void)memset(&result, 0, sizeof(result));
    result.frame_counter = frame_counter;
    if (tx_mutex_get(&g_thermal_ai_mutex, TX_WAIT_FOREVER) == TX_SUCCESS)
    {
      thermal_ai_runtime_reset(&g_thermal_ai_runtime);
      g_thermal_ai_model_ready = 0U;
      g_thermal_ai_diag_stage = 0U;
      tx_mutex_put(&g_thermal_ai_mutex);
    }
    return;
  }

  (void)memset(&result, 0, sizeof(result));
  result.frame_counter = frame_counter;
  result.max_temp_centi_c = (uint16_t)app_threadx_temp14_to_centi_celsius(max_temp14);
  result.ambient_temp_centi_c = (uint16_t)app_threadx_temp14_to_centi_celsius(background_temp14);
  app_threadx_thermal_ai_set_diag_stage(12U, 0U);
  app_threadx_thermal_ai_decode_output(&result);
#if (CFG_THERMAL_AI_HOTSPOT_FALLBACK_ENABLE != 0U)
  app_threadx_thermal_ai_append_hotspot_fallback(&result, background_temp14);
#endif

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
    g_thermal_ai_diag_stage = 0U;
    tx_mutex_put(&g_thermal_ai_mutex);
  }
}

/**
  * @brief  Return one LVGL color for the requested AI class.
  * @param  class_id Detection class identifier.
  * @retval uint16_t RGB565-compatible LVGL color value.
  */
static lv_color_t app_threadx_thermal_ai_color_for_class(uint8_t class_id)
{
  switch ((thermal_ai_class_t)class_id)
  {
    case THERMAL_AI_CLASS_CIRCUIT_BOARD_NORMAL:
      return lv_palette_main(LV_PALETTE_BLUE);
    case THERMAL_AI_CLASS_CIRCUIT_BOARD_ABNORMAL_HOTSPOT:
      return lv_palette_main(LV_PALETTE_YELLOW);
    default:
      return lv_palette_main(LV_PALETTE_GREY);
  }
}

/**
  * @brief  Detect one abnormal hotspot directly from the current RGB565 preview.
  * @param  frame_counter Current thermal frame counter.
  * @retval None
  */
static void app_threadx_thermal_ai_run_preview_hotspot(const uint16_t *temp14_frame, uint32_t frame_counter)
{
  thermal_ai_result_t result;
  uint16_t background_temp14;
  ULONG tick_start;

  (void)memset(&result, 0, sizeof(result));
  result.frame_counter = frame_counter;

  tick_start = HAL_GetTick();
  background_temp14 = app_threadx_thermal_ai_prepare_preview_temp14(temp14_frame);
#if (CFG_THERMAL_AI_HOTSPOT_FALLBACK_ENABLE != 0U)
  app_threadx_thermal_ai_append_hotspot_fallback(&result, background_temp14);
#endif
  g_thermal_ai_last_inference_ms = HAL_GetTick() - tick_start;

  g_thermal_ai_diag_run_count++;
  g_thermal_ai_diag_stage = 0U;
  g_thermal_ai_diag_raw_candidate_count = result.detection_count;
  g_thermal_ai_diag_postfilter_candidate_count = result.detection_count;
  g_thermal_ai_diag_max_score_permille =
      (result.detection_count != 0U) ? result.detections[0].confidence_permille : 0U;
  g_thermal_ai_diag_max_objectness_permille = g_thermal_ai_diag_max_score_permille;

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
  * @brief  Convert one hotspot-fallback candidate into a bounded confidence value.
  * @param  preview_guided Non-zero when the preview-image path found the hotspot.
  * @param  hot_pixel_count Count of pixels inside the hotspot candidate.
  * @param  box_width Bounding-box width in model-input pixels.
  * @param  box_height Bounding-box height in model-input pixels.
  * @param  background_score Median RGB565 hotness score.
  * @param  peak_score Peak RGB565 hotness score inside the candidate.
  * @retval uint16_t Confidence in permille for display/runtime overlay.
  */
static uint16_t app_threadx_thermal_ai_calc_hotspot_fallback_confidence(uint8_t preview_guided,
                                                                        uint32_t hot_pixel_count,
                                                                        uint32_t box_width,
                                                                        uint32_t box_height,
                                                                        uint8_t background_score,
                                                                        uint8_t peak_score)
{
  uint32_t box_area;
  uint32_t fill_permille;
  uint32_t contrast_score = 0U;
  uint32_t contrast_term;
  uint32_t area_term;
  uint32_t fill_term;
  uint32_t confidence_permille;

  TX_PARAMETER_NOT_USED(preview_guided);

  box_area = box_width * box_height;
  if (box_area == 0U)
  {
    return CFG_THERMAL_AI_HOTSPOT_FALLBACK_CONFIDENCE_MIN_PERMILLE;
  }

  fill_permille = (hot_pixel_count >= box_area) ? 1000U : ((hot_pixel_count * 1000U) / box_area);

  if (peak_score > background_score)
  {
    contrast_score = (uint32_t)peak_score - (uint32_t)background_score;
  }
  if (contrast_score > 180U)
  {
    contrast_score = 180U;
  }

  contrast_term = contrast_score * 2U;
  fill_term = fill_permille / 8U;
  area_term = hot_pixel_count / 32U;
  if (area_term > 50U)
  {
    area_term = 50U;
  }

  confidence_permille = 400U;
  confidence_permille += contrast_term;
  confidence_permille += fill_term;
  confidence_permille += area_term;

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
  * @brief  Create runtime AI overlay rectangles on both preview images.
  * @retval None
  */
static void __attribute__((unused)) app_threadx_gui_init_ai_overlays(void)
{
  if (g_thermal_ai_overlay_ready != 0U)
  {
    return;
  }

  app_threadx_gui_init_ai_overlay_set(guider_ui.WidgetsDemo_preview_frame,
                                      g_thermal_ai_preview_boxes,
                                      g_thermal_ai_preview_labels);
  app_threadx_gui_init_ai_overlay_set(guider_ui.WidgetsDemo_fullscreen_preview_frame,
                                      g_thermal_ai_fullscreen_boxes,
                                      g_thermal_ai_fullscreen_labels);
  g_thermal_ai_overlay_ready = 1U;
}

/**
  * @brief  Create one set of AI overlay rectangles for one preview frame.
  * @param  parent_obj Parent preview frame object.
  * @param  box_array Output box-object array.
  * @param  label_array Output label-object array.
  * @retval None
  */
static void app_threadx_gui_init_ai_overlay_set(lv_obj_t *parent_obj,
                                                lv_obj_t **box_array,
                                                lv_obj_t **label_array)
{
  uint32_t detection_index;

  if ((parent_obj == NULL) || (box_array == NULL) || (label_array == NULL))
  {
    return;
  }

  for (detection_index = 0U; detection_index < CFG_THERMAL_AI_RUNTIME_MAX_DETECTIONS; detection_index++)
  {
    lv_obj_t *box = lv_obj_create(parent_obj);
    lv_obj_t *label = lv_label_create(box);

    box_array[detection_index] = box;
    label_array[detection_index] = label;

    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(box, 3, 0);
    lv_obj_set_style_border_color(box, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_border_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 0, 0);
    lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_style_bg_opa(label, LV_OPA_60, 0);
    lv_obj_set_style_bg_color(label, lv_color_black(), 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_label_set_text(label, "");
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
  }
}

/**
  * @brief  Update one set of AI overlay rectangles from the latest decoded detections.
  * @param  box_array Box-object array.
  * @param  label_array Label-object array.
  * @param  preview_width Target preview width in pixels.
  * @param  preview_height Target preview height in pixels.
  * @param  result_ptr Latest decoded AI result, may be NULL.
  * @retval None
  */
static void __attribute__((unused)) app_threadx_gui_update_ai_overlay_set(lv_obj_t **box_array,
                                                  lv_obj_t **label_array,
                                                  int32_t origin_x,
                                                  int32_t origin_y,
                                                  uint16_t preview_width,
                                                  uint16_t preview_height,
                                                  const thermal_ai_result_t *result_ptr,
                                                  uint8_t hide_unconfirmed_abnormal)
{
  uint32_t detection_index;

  if ((box_array == NULL) || (label_array == NULL))
  {
    return;
  }

  for (detection_index = 0U; detection_index < CFG_THERMAL_AI_RUNTIME_MAX_DETECTIONS; detection_index++)
  {
    lv_obj_t *box = box_array[detection_index];
    lv_obj_t *label = label_array[detection_index];
    uint8_t hide_detection = 0U;

    if ((box == NULL) || (label == NULL))
    {
      continue;
    }

    if ((result_ptr == NULL) ||
        (detection_index >= result_ptr->detection_count) ||
        (result_ptr->detections[detection_index].valid == 0U))
    {
      hide_detection = 1U;
    }
#if (CFG_THERMAL_AI_DRAW_NORMAL_BOXES == 0U)
    else if (result_ptr->detections[detection_index].class_id == (uint8_t)THERMAL_AI_CLASS_CIRCUIT_BOARD_NORMAL)
    {
      hide_detection = 1U;
    }
#endif
    else if ((hide_unconfirmed_abnormal != 0U) &&
             (result_ptr->detections[detection_index].class_id == (uint8_t)THERMAL_AI_CLASS_CIRCUIT_BOARD_ABNORMAL_HOTSPOT))
    {
      hide_detection = 1U;
    }

    if (hide_detection != 0U)
    {
      lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    {
      thermal_ai_bbox_t scaled_bbox;
      const thermal_ai_detection_t *detection_ptr = &result_ptr->detections[detection_index];
      char label_text[32];
      lv_color_t border_color = app_threadx_thermal_ai_color_for_class(detection_ptr->class_id);
      lv_coord_t box_x;
      lv_coord_t box_y;
      lv_coord_t box_w;
      lv_coord_t box_h;
      int32_t image_x_min;
      int32_t image_y_min;
      int32_t image_x_max;
      int32_t image_y_max;
      int32_t center_x;
      int32_t center_y;
      int32_t min_box_size = (int32_t)CFG_THERMAL_AI_MIN_DISPLAY_BOX_PX;

      thermal_ai_runtime_scale_bbox(&detection_ptr->bbox,
                                    CFG_THERMAL_AI_INPUT_WIDTH,
                                    CFG_THERMAL_AI_INPUT_HEIGHT,
                                    preview_width,
                                    preview_height,
                                    &scaled_bbox);
      image_x_min = origin_x;
      image_y_min = origin_y;
      image_x_max = origin_x + (int32_t)preview_width;
      image_y_max = origin_y + (int32_t)preview_height;
      box_x = (lv_coord_t)(origin_x + (int32_t)scaled_bbox.x_min);
      box_y = (lv_coord_t)(origin_y + (int32_t)scaled_bbox.y_min);
      box_w = (lv_coord_t)((scaled_bbox.x_max > scaled_bbox.x_min) ? (scaled_bbox.x_max - scaled_bbox.x_min) : 1U);
      box_h = (lv_coord_t)((scaled_bbox.y_max > scaled_bbox.y_min) ? (scaled_bbox.y_max - scaled_bbox.y_min) : 1U);
      if ((int32_t)box_w < min_box_size)
      {
        center_x = origin_x + (((int32_t)scaled_bbox.x_min + (int32_t)scaled_bbox.x_max) / 2);
        box_w = (lv_coord_t)min_box_size;
        box_x = (lv_coord_t)(center_x - (min_box_size / 2));
      }
      if ((int32_t)box_h < min_box_size)
      {
        center_y = origin_y + (((int32_t)scaled_bbox.y_min + (int32_t)scaled_bbox.y_max) / 2);
        box_h = (lv_coord_t)min_box_size;
        box_y = (lv_coord_t)(center_y - (min_box_size / 2));
      }
      if ((int32_t)box_w > (int32_t)preview_width)
      {
        box_w = (lv_coord_t)preview_width;
      }
      if ((int32_t)box_h > (int32_t)preview_height)
      {
        box_h = (lv_coord_t)preview_height;
      }
      if ((int32_t)box_x < image_x_min)
      {
        box_x = (lv_coord_t)image_x_min;
      }
      if ((int32_t)box_y < image_y_min)
      {
        box_y = (lv_coord_t)image_y_min;
      }
      if (((int32_t)box_x + (int32_t)box_w) > image_x_max)
      {
        box_x = (lv_coord_t)(image_x_max - (int32_t)box_w);
      }
      if (((int32_t)box_y + (int32_t)box_h) > image_y_max)
      {
        box_y = (lv_coord_t)(image_y_max - (int32_t)box_h);
      }
      lv_obj_clear_flag(box, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_pos(box, box_x, box_y);
      lv_obj_set_size(box, box_w, box_h);
      lv_obj_set_style_border_color(box, border_color, 0);
      lv_obj_move_foreground(box);
      (void)snprintf(label_text,
                     sizeof(label_text),
                     "HOT %u%%",
                     (unsigned int)((detection_ptr->confidence_permille + 5U) / 10U));
      lv_label_set_text(label, label_text);
      lv_obj_set_style_border_color(label, border_color, 0);
    }
  }
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
  * @brief  GUI rendering thread entry.
  * @param  thread_input Unused thread input parameter.
  * @retval None
  */
static VOID app_threadx_gui_thread_entry(ULONG thread_input)
{
  uint16_t preview_width;
  uint16_t preview_height;
  uint32_t frame_counter_last = 0U;
  uint32_t overlay_update_tick_last = 0U;

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

  preview_width = tiny1c_thermal_app_get_preview_width();
  preview_height = tiny1c_thermal_app_get_preview_height();
  g_gui_preview_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
  g_gui_preview_img_dsc.header.always_zero = 0U;
  app_threadx_gui_build_scaled_frame(tiny1c_thermal_app_get_rgb565_frame(),
                                    g_gui_preview_rgb565_frame,
                                    preview_width,
                                    preview_height,
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
#if (CFG_THERMAL_AI_RGB565_BOX_COMPOSITE == 0U)
  if (CFG_THERMAL_AI_ENABLE != 0U)
  {
    app_threadx_gui_init_ai_overlays();
  }
#endif

  for (;;)
  {
    uint32_t frame_counter_now;
    uint32_t now_ms;
    thermal_ai_result_t ai_draw_result;
    char time_text[16];
    char battery_text[16];
    char ai_text[24];
    uint32_t seconds;
    uint8_t abnormal_detected = 0U;

    frame_counter_now = tiny1c_thermal_app_get_frame_counter();
    if ((frame_counter_now != 0U) && (frame_counter_now != frame_counter_last))
    {
      uint8_t ai_draw_result_valid = app_threadx_thermal_ai_get_result_snapshot(&ai_draw_result);

      frame_counter_last = frame_counter_now;
      if (thermal_gui_is_fullscreen_active() != 0U)
      {
        app_threadx_gui_update_fullscreen_image();
#if (CFG_THERMAL_AI_RGB565_BOX_COMPOSITE != 0U)
        if (ai_draw_result_valid != 0U)
        {
          app_threadx_gui_draw_ai_boxes_rgb565(g_gui_fullscreen_rgb565_frame,
                                                CFG_GUI_FULLSCREEN_WIDTH,
                                                CFG_GUI_FULLSCREEN_HEIGHT,
                                                &ai_draw_result);
        }
#endif
        lv_obj_invalidate(guider_ui.WidgetsDemo_fullscreen_preview_img);
      }
      else
      {
        app_threadx_gui_build_scaled_frame(tiny1c_thermal_app_get_rgb565_frame(),
                                          g_gui_preview_rgb565_frame,
                                          tiny1c_thermal_app_get_preview_width(),
                                          tiny1c_thermal_app_get_preview_height(),
                                          CFG_GUI_PREVIEW_WIDTH,
                                          CFG_GUI_PREVIEW_HEIGHT);
#if (CFG_THERMAL_AI_RGB565_BOX_COMPOSITE != 0U)
        if (ai_draw_result_valid != 0U)
        {
          app_threadx_gui_draw_ai_boxes_rgb565(g_gui_preview_rgb565_frame,
                                                CFG_GUI_PREVIEW_WIDTH,
                                                CFG_GUI_PREVIEW_HEIGHT,
                                                &ai_draw_result);
        }
#endif
        lv_obj_invalidate(guider_ui.WidgetsDemo_preview_img);
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
  * @brief  Extend the Web file-mode inactivity deadline during an active transfer.
  * @retval None
  */
static void app_threadx_uart_file_web_timeout_refresh(void)
{
  if ((g_uart_file_hold_mask & APP_UART_FILE_HOLD_WEB) != 0U)
  {
    g_uart_web_file_hold_expire_tick = tx_time_get() +
      ((CFG_UART_FILE_WEB_TIMEOUT_TICKS > 0U) ? CFG_UART_FILE_WEB_TIMEOUT_TICKS : 1U);
  }
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

    app_threadx_uart_file_web_timeout_refresh();
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
    app_threadx_uart_file_web_timeout_refresh();
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
