/**
  ******************************************************************************
  * @file    thermal_ai_runtime.h
  * @brief   Thermal AI runtime helpers for detection, alarm, and UI integration.
  ******************************************************************************
  */

#ifndef THERMAL_AI_RUNTIME_H
#define THERMAL_AI_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define CFG_THERMAL_AI_RUNTIME_MAX_DETECTIONS    8U
#define CFG_THERMAL_AI_RUNTIME_MIN_CONFIRM_FRAMES 1U
#define CFG_THERMAL_AI_RUNTIME_ABNORMAL_MIN_CONFIDENCE_PERMILLE 980U

typedef enum
{
    THERMAL_AI_CLASS_NONE = 0U,
    THERMAL_AI_CLASS_CIRCUIT_BOARD_NORMAL = 1U,
    THERMAL_AI_CLASS_CIRCUIT_BOARD_ABNORMAL_HOTSPOT = 2U
} thermal_ai_class_t;

typedef enum
{
    THERMAL_AI_MODE_BOARD_INSPECTION = 0U
} thermal_ai_app_mode_t;

typedef enum
{
    THERMAL_AI_ALARM_IDLE = 0U,
    THERMAL_AI_ALARM_PENDING = 1U,
    THERMAL_AI_ALARM_ACTIVE = 2U
} thermal_ai_alarm_state_t;

typedef struct
{
    uint16_t x;
    uint16_t y;
} thermal_ai_point_t;

typedef struct
{
    uint16_t x_min;
    uint16_t y_min;
    uint16_t x_max;
    uint16_t y_max;
} thermal_ai_bbox_t;

typedef struct
{
    uint8_t valid;
    uint8_t class_id;
    uint16_t confidence_permille;
    thermal_ai_bbox_t bbox;
} thermal_ai_detection_t;

typedef struct
{
    uint32_t frame_counter;
    uint8_t detection_count;
    uint8_t reserved0;
    uint16_t max_temp_centi_c;
    uint16_t ambient_temp_centi_c;
    thermal_ai_detection_t detections[CFG_THERMAL_AI_RUNTIME_MAX_DETECTIONS];
} thermal_ai_result_t;

typedef struct
{
    thermal_ai_app_mode_t mode;
    thermal_ai_alarm_state_t alarm_state;
    uint8_t confirm_frame_threshold;
    uint8_t consecutive_abnormal_frames;
    uint8_t capture_latch;
    uint8_t reserved0;
    int32_t temp_delta_threshold_centi_c;
    uint32_t last_alarm_frame_counter;
    thermal_ai_result_t last_result;
} thermal_ai_runtime_t;

/**
 * @brief Initialize one runtime instance with default state.
 * @param runtime_ptr Runtime state object.
 * @param confirm_frame_threshold Number of consecutive abnormal frames required before alarm activation.
 * @param temp_delta_threshold_centi_c Minimum abnormal temperature delta in 0.01 degC units.
 * @return None.
 */
void thermal_ai_runtime_init(thermal_ai_runtime_t *runtime_ptr,
                             uint8_t confirm_frame_threshold,
                             int32_t temp_delta_threshold_centi_c);

/**
 * @brief Clear all runtime state and stored detections.
 * @param runtime_ptr Runtime state object.
 * @return None.
 */
void thermal_ai_runtime_reset(thermal_ai_runtime_t *runtime_ptr);

/**
 * @brief Set the current application mode.
 * @param runtime_ptr Runtime state object.
 * @param mode New application mode.
 * @return None.
 */
void thermal_ai_runtime_set_mode(thermal_ai_runtime_t *runtime_ptr, thermal_ai_app_mode_t mode);

/**
 * @brief Get the current application mode.
 * @param runtime_ptr Runtime state object.
 * @return thermal_ai_app_mode_t Active application mode.
 */
thermal_ai_app_mode_t thermal_ai_runtime_get_mode(const thermal_ai_runtime_t *runtime_ptr);

/**
 * @brief Return a short static display string for one application mode.
 * @param mode Application mode.
 * @return const char* ASCII mode name.
 */
const char *thermal_ai_runtime_get_mode_name(thermal_ai_app_mode_t mode);

/**
 * @brief Return a short static display string for one AI class.
 * @param class_id Detection class identifier.
 * @return const char* ASCII class name.
 */
const char *thermal_ai_runtime_get_class_name(uint8_t class_id);

/**
 * @brief Store one fresh AI result and update alarm state.
 * @param runtime_ptr Runtime state object.
 * @param result_ptr Latest AI result.
 * @return None.
 */
void thermal_ai_runtime_update(thermal_ai_runtime_t *runtime_ptr,
                               const thermal_ai_result_t *result_ptr);

/**
 * @brief Return whether the current result contains one detection of the requested class.
 * @param result_ptr AI result object.
 * @param class_id Detection class identifier.
 * @param min_confidence_permille Minimum confidence threshold in 0..1000.
 * @return uint8_t Non-zero when at least one matching detection exists.
 */
uint8_t thermal_ai_runtime_result_has_class(const thermal_ai_result_t *result_ptr,
                                            uint8_t class_id,
                                            uint16_t min_confidence_permille);

/**
 * @brief Return whether the alarm state is currently active.
 * @param runtime_ptr Runtime state object.
 * @return uint8_t Non-zero when abnormal alarm is active.
 */
uint8_t thermal_ai_runtime_is_alarm_active(const thermal_ai_runtime_t *runtime_ptr);

/**
 * @brief Return whether an alarm-triggered evidence capture should be performed.
 * @param runtime_ptr Runtime state object.
 * @return uint8_t Non-zero when a new capture should be triggered.
 */
uint8_t thermal_ai_runtime_should_capture(const thermal_ai_runtime_t *runtime_ptr);

/**
 * @brief Clear the one-shot capture latch after evidence storage is completed.
 * @param runtime_ptr Runtime state object.
 * @return None.
 */
void thermal_ai_runtime_clear_capture_latch(thermal_ai_runtime_t *runtime_ptr);

/**
 * @brief Reset only the abnormal alarm sub-state.
 * @param runtime_ptr Runtime state object.
 * @return None.
 */
void thermal_ai_runtime_reset_alarm(thermal_ai_runtime_t *runtime_ptr);

/**
 * @brief Scale one point from frame coordinates into preview coordinates.
 * @param source_x Source X in frame space.
 * @param source_y Source Y in frame space.
 * @param frame_width Source frame width.
 * @param frame_height Source frame height.
 * @param preview_width Destination preview width.
 * @param preview_height Destination preview height.
 * @param dest_x_ptr Output preview X pointer, may be NULL.
 * @param dest_y_ptr Output preview Y pointer, may be NULL.
 * @return None.
 */
void thermal_ai_runtime_scale_point(uint16_t source_x,
                                    uint16_t source_y,
                                    uint16_t frame_width,
                                    uint16_t frame_height,
                                    uint16_t preview_width,
                                    uint16_t preview_height,
                                    uint16_t *dest_x_ptr,
                                    uint16_t *dest_y_ptr);

/**
 * @brief Scale one bounding box from frame coordinates into preview coordinates.
 * @param source_bbox_ptr Source bounding box.
 * @param frame_width Source frame width.
 * @param frame_height Source frame height.
 * @param preview_width Destination preview width.
 * @param preview_height Destination preview height.
 * @param dest_bbox_ptr Output preview bounding box.
 * @return None.
 */
void thermal_ai_runtime_scale_bbox(const thermal_ai_bbox_t *source_bbox_ptr,
                                   uint16_t frame_width,
                                   uint16_t frame_height,
                                   uint16_t preview_width,
                                   uint16_t preview_height,
                                   thermal_ai_bbox_t *dest_bbox_ptr);

#ifdef __cplusplus
}
#endif

#endif /* THERMAL_AI_RUNTIME_H */
