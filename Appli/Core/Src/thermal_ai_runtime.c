/**
  ******************************************************************************
  * @file    thermal_ai_runtime.c
  * @brief   Thermal AI runtime helpers for detection, alarm, and UI integration.
  ******************************************************************************
  */

#include "thermal_ai_runtime.h"

#include <string.h>

/**
 * @brief Clamp one unsigned value into a closed range.
 * @param value Input value.
 * @param min_value Inclusive lower bound.
 * @param max_value Inclusive upper bound.
 * @return uint16_t Clamped value.
 */
static uint16_t thermal_ai_runtime_clamp_u16(uint16_t value, uint16_t min_value, uint16_t max_value)
{
    if (value < min_value)
    {
        return min_value;
    }

    if (value > max_value)
    {
        return max_value;
    }

    return value;
}

/**
 * @brief Return whether the current mode should treat abnormal board hotspots as alarm sources.
 * @param mode Current application mode.
 * @return uint8_t Non-zero when abnormal board detections should drive alarm state.
 */
static uint8_t thermal_ai_runtime_mode_uses_board_alarm(thermal_ai_app_mode_t mode)
{
    return (mode == THERMAL_AI_MODE_BOARD_INSPECTION) ? 1U : 0U;
}

void thermal_ai_runtime_init(thermal_ai_runtime_t *runtime_ptr,
                             uint8_t confirm_frame_threshold,
                             int32_t temp_delta_threshold_centi_c)
{
    if (runtime_ptr == NULL)
    {
        return;
    }

    (void)memset(runtime_ptr, 0, sizeof(*runtime_ptr));
    runtime_ptr->mode = THERMAL_AI_MODE_BOARD_INSPECTION;
    runtime_ptr->alarm_state = THERMAL_AI_ALARM_IDLE;
    runtime_ptr->confirm_frame_threshold = (confirm_frame_threshold < CFG_THERMAL_AI_RUNTIME_MIN_CONFIRM_FRAMES)
                                               ? CFG_THERMAL_AI_RUNTIME_MIN_CONFIRM_FRAMES
                                               : confirm_frame_threshold;
    runtime_ptr->temp_delta_threshold_centi_c = temp_delta_threshold_centi_c;
}

void thermal_ai_runtime_reset(thermal_ai_runtime_t *runtime_ptr)
{
    if (runtime_ptr == NULL)
    {
        return;
    }

    thermal_ai_runtime_init(runtime_ptr,
                            runtime_ptr->confirm_frame_threshold,
                            runtime_ptr->temp_delta_threshold_centi_c);
}

void thermal_ai_runtime_set_mode(thermal_ai_runtime_t *runtime_ptr, thermal_ai_app_mode_t mode)
{
    if (runtime_ptr == NULL)
    {
        return;
    }

    runtime_ptr->mode = mode;
    thermal_ai_runtime_reset_alarm(runtime_ptr);
}

thermal_ai_app_mode_t thermal_ai_runtime_get_mode(const thermal_ai_runtime_t *runtime_ptr)
{
    if (runtime_ptr == NULL)
    {
        return THERMAL_AI_MODE_BOARD_INSPECTION;
    }

    return runtime_ptr->mode;
}

const char *thermal_ai_runtime_get_mode_name(thermal_ai_app_mode_t mode)
{
    switch (mode)
    {
        case THERMAL_AI_MODE_BOARD_INSPECTION:
        default:
            return "board";
    }
}

const char *thermal_ai_runtime_get_class_name(uint8_t class_id)
{
    switch ((thermal_ai_class_t)class_id)
    {
        case THERMAL_AI_CLASS_CIRCUIT_BOARD_NORMAL:
            return "board_normal";
        case THERMAL_AI_CLASS_CIRCUIT_BOARD_ABNORMAL_HOTSPOT:
            return "board_abnormal";
        case THERMAL_AI_CLASS_NONE:
        default:
            return "none";
    }
}

uint8_t thermal_ai_runtime_result_has_class(const thermal_ai_result_t *result_ptr,
                                            uint8_t class_id,
                                            uint16_t min_confidence_permille)
{
    uint8_t detection_index;

    if (result_ptr == NULL)
    {
        return 0U;
    }

    for (detection_index = 0U; detection_index < result_ptr->detection_count; detection_index++)
    {
        const thermal_ai_detection_t *detection_ptr = &result_ptr->detections[detection_index];
        if ((detection_ptr->valid != 0U) &&
            (detection_ptr->class_id == class_id) &&
            (detection_ptr->confidence_permille >= min_confidence_permille))
        {
            return 1U;
        }
    }

    return 0U;
}

void thermal_ai_runtime_update(thermal_ai_runtime_t *runtime_ptr,
                               const thermal_ai_result_t *result_ptr)
{
    uint8_t abnormal_detected = 0U;
    int32_t temp_delta_centi_c;

    if ((runtime_ptr == NULL) || (result_ptr == NULL))
    {
        return;
    }

    runtime_ptr->last_result = *result_ptr;

    if (thermal_ai_runtime_mode_uses_board_alarm(runtime_ptr->mode) != 0U)
    {
        abnormal_detected = thermal_ai_runtime_result_has_class(
            result_ptr,
            (uint8_t)THERMAL_AI_CLASS_CIRCUIT_BOARD_ABNORMAL_HOTSPOT,
            CFG_THERMAL_AI_RUNTIME_ABNORMAL_MIN_CONFIDENCE_PERMILLE);
    }

    temp_delta_centi_c = (int32_t)result_ptr->max_temp_centi_c - (int32_t)result_ptr->ambient_temp_centi_c;
    if ((abnormal_detected != 0U) && (temp_delta_centi_c >= runtime_ptr->temp_delta_threshold_centi_c))
    {
        if (runtime_ptr->consecutive_abnormal_frames < 255U)
        {
            runtime_ptr->consecutive_abnormal_frames++;
        }

        if (runtime_ptr->consecutive_abnormal_frames >= runtime_ptr->confirm_frame_threshold)
        {
            if (runtime_ptr->alarm_state != THERMAL_AI_ALARM_ACTIVE)
            {
                runtime_ptr->capture_latch = 1U;
            }
            runtime_ptr->alarm_state = THERMAL_AI_ALARM_ACTIVE;
            runtime_ptr->last_alarm_frame_counter = result_ptr->frame_counter;
        }
        else
        {
            runtime_ptr->alarm_state = THERMAL_AI_ALARM_PENDING;
        }
    }
    else
    {
        runtime_ptr->consecutive_abnormal_frames = 0U;
        runtime_ptr->alarm_state = THERMAL_AI_ALARM_IDLE;
        runtime_ptr->capture_latch = 0U;
    }
}

uint8_t thermal_ai_runtime_is_alarm_active(const thermal_ai_runtime_t *runtime_ptr)
{
    if (runtime_ptr == NULL)
    {
        return 0U;
    }

    return (runtime_ptr->alarm_state == THERMAL_AI_ALARM_ACTIVE) ? 1U : 0U;
}

uint8_t thermal_ai_runtime_should_capture(const thermal_ai_runtime_t *runtime_ptr)
{
    if (runtime_ptr == NULL)
    {
        return 0U;
    }

    return runtime_ptr->capture_latch;
}

void thermal_ai_runtime_clear_capture_latch(thermal_ai_runtime_t *runtime_ptr)
{
    if (runtime_ptr == NULL)
    {
        return;
    }

    runtime_ptr->capture_latch = 0U;
}

void thermal_ai_runtime_reset_alarm(thermal_ai_runtime_t *runtime_ptr)
{
    if (runtime_ptr == NULL)
    {
        return;
    }

    runtime_ptr->alarm_state = THERMAL_AI_ALARM_IDLE;
    runtime_ptr->consecutive_abnormal_frames = 0U;
    runtime_ptr->capture_latch = 0U;
    runtime_ptr->last_alarm_frame_counter = 0U;
}

void thermal_ai_runtime_scale_point(uint16_t source_x,
                                    uint16_t source_y,
                                    uint16_t frame_width,
                                    uint16_t frame_height,
                                    uint16_t preview_width,
                                    uint16_t preview_height,
                                    uint16_t *dest_x_ptr,
                                    uint16_t *dest_y_ptr)
{
    uint32_t scaled_x;
    uint32_t scaled_y;

    if ((frame_width == 0U) || (frame_height == 0U) || (preview_width == 0U) || (preview_height == 0U))
    {
        if (dest_x_ptr != NULL)
        {
            *dest_x_ptr = 0U;
        }
        if (dest_y_ptr != NULL)
        {
            *dest_y_ptr = 0U;
        }
        return;
    }

    source_x = thermal_ai_runtime_clamp_u16(source_x, 0U, (uint16_t)(frame_width - 1U));
    source_y = thermal_ai_runtime_clamp_u16(source_y, 0U, (uint16_t)(frame_height - 1U));

    scaled_x = ((uint32_t)source_x * (uint32_t)preview_width) / (uint32_t)frame_width;
    scaled_y = ((uint32_t)source_y * (uint32_t)preview_height) / (uint32_t)frame_height;

    if (dest_x_ptr != NULL)
    {
        *dest_x_ptr = thermal_ai_runtime_clamp_u16((uint16_t)scaled_x, 0U, (uint16_t)(preview_width - 1U));
    }

    if (dest_y_ptr != NULL)
    {
        *dest_y_ptr = thermal_ai_runtime_clamp_u16((uint16_t)scaled_y, 0U, (uint16_t)(preview_height - 1U));
    }
}

void thermal_ai_runtime_scale_bbox(const thermal_ai_bbox_t *source_bbox_ptr,
                                   uint16_t frame_width,
                                   uint16_t frame_height,
                                   uint16_t preview_width,
                                   uint16_t preview_height,
                                   thermal_ai_bbox_t *dest_bbox_ptr)
{
    if ((source_bbox_ptr == NULL) || (dest_bbox_ptr == NULL))
    {
        return;
    }

    thermal_ai_runtime_scale_point(source_bbox_ptr->x_min,
                                   source_bbox_ptr->y_min,
                                   frame_width,
                                   frame_height,
                                   preview_width,
                                   preview_height,
                                   &dest_bbox_ptr->x_min,
                                   &dest_bbox_ptr->y_min);
    thermal_ai_runtime_scale_point(source_bbox_ptr->x_max,
                                   source_bbox_ptr->y_max,
                                   frame_width,
                                   frame_height,
                                   preview_width,
                                   preview_height,
                                   &dest_bbox_ptr->x_max,
                                   &dest_bbox_ptr->y_max);

    if (dest_bbox_ptr->x_max <= dest_bbox_ptr->x_min)
    {
        dest_bbox_ptr->x_max = (uint16_t)(dest_bbox_ptr->x_min + 1U);
    }

    if (dest_bbox_ptr->y_max <= dest_bbox_ptr->y_min)
    {
        dest_bbox_ptr->y_max = (uint16_t)(dest_bbox_ptr->y_min + 1U);
    }
}
