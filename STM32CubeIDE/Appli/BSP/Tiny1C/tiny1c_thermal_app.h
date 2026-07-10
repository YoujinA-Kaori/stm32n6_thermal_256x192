#ifndef TINY1C_THERMAL_APP_H
#define TINY1C_THERMAL_APP_H

#include <stdint.h>
#include "dcmipp.h"
#include "tiny1c_vdcmd_app.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the Tiny1-C temperature streaming application.
 * @param None
 * @return ir_error_t `IR_SUCCESS` indicates the startup completed successfully.
 */
ir_error_t tiny1c_thermal_app_start(void);

/**
 * @brief Poll and process the latest Tiny1-C temperature frame.
 * @param None
 * @return None
 */
void tiny1c_thermal_app_process(void);

/**
 * @brief Handle the DCMIPP frame-complete callback for the temperature stream.
 * @param dcmipp_handle Pointer to the DCMIPP handle.
 * @param pipe DCMIPP pipe index.
 * @return None
 */
void tiny1c_thermal_app_on_frame_event(DCMIPP_HandleTypeDef *dcmipp_handle, uint32_t pipe);

/**
 * @brief Get the latest decoded Y14 temperature frame buffer.
 * @param None
 * @return const uint16_t* Pointer to the Y14 frame buffer.
 */
const uint16_t *tiny1c_thermal_app_get_temp14_frame(void);

/**
 * @brief Get the temperature frame width.
 * @param None
 * @return uint16_t Frame width in pixels.
 */
uint16_t tiny1c_thermal_app_get_frame_width(void);

/**
 * @brief Get the temperature frame height.
 * @param None
 * @return uint16_t Frame height in pixels.
 */
uint16_t tiny1c_thermal_app_get_frame_height(void);

/**
 * @brief Get the number of frames that have been decoded and rendered.
 * @param None
 * @return uint32_t Processed frame counter.
 */
uint32_t tiny1c_thermal_app_get_frame_counter(void);

/**
 * @brief Override the center temperature overlay value in 0.01 degrees Celsius.
 * @param center_temp_centi_c Compensated center temperature in centi-degrees Celsius.
 * @return None
 */
void tiny1c_thermal_app_set_center_temp_centi_c(int32_t center_temp_centi_c);

/**
 * @brief Get the RGB565 preview buffer rendered from the latest thermal frame.
 * @param None
 * @return const uint16_t* Pointer to the RGB565 preview buffer.
 */
const uint16_t *tiny1c_thermal_app_get_rgb565_frame(void);

/**
 * @brief Get the preview buffer width in pixels.
 * @param None
 * @return uint16_t Preview width.
 */
uint16_t tiny1c_thermal_app_get_preview_width(void);

/**
 * @brief Get the preview buffer height in pixels.
 * @param None
 * @return uint16_t Preview height.
 */
uint16_t tiny1c_thermal_app_get_preview_height(void);

/**
 * @brief Get the current center temperature overlay value in 0.01 degrees Celsius.
 * @param None
 * @return int32_t Center temperature in centi-degrees Celsius.
 */
int32_t tiny1c_thermal_app_get_center_temp_centi_c(void);

/**
 * @brief Trigger one manual Tiny1-C FFC/B-update cycle.
 * @param None
 * @return ir_error_t `IR_SUCCESS` indicates the request was accepted.
 */
ir_error_t tiny1c_thermal_app_force_ffc(void);

/**
 * @brief Set preview mirror/flip flags for the rendered RGB565 frame.
 * @param mirror_enable Non-zero to mirror horizontally.
 * @param flip_enable Non-zero to flip vertically.
 * @return None
 */
void tiny1c_thermal_app_set_preview_transform(uint8_t mirror_enable, uint8_t flip_enable);

/**
 * @brief Set preview contrast level from the GUI slider.
 * @param contrast_slider_value Slider value in range 0..100.
 * @return None
 */
void tiny1c_thermal_app_set_preview_contrast(uint8_t contrast_slider_value);

/**
 * @brief Get whether preview horizontal mirror is enabled.
 * @param None
 * @return uint8_t Non-zero when horizontal mirror is enabled.
 */
uint8_t tiny1c_thermal_app_get_preview_mirror_enabled(void);

/**
 * @brief Get whether preview vertical flip is enabled.
 * @param None
 * @return uint8_t Non-zero when vertical flip is enabled.
 */
uint8_t tiny1c_thermal_app_get_preview_flip_enabled(void);

/**
 * @brief Map one raw temp14 frame coordinate into the current preview-oriented frame coordinate.
 * @param source_x Source-frame X coordinate in range 0..255.
 * @param source_y Source-frame Y coordinate in range 0..191.
 * @param dest_x Pointer to the transformed X coordinate, may be NULL.
 * @param dest_y Pointer to the transformed Y coordinate, may be NULL.
 * @return None
 */
void tiny1c_thermal_app_transform_frame_point(uint16_t source_x,
                                              uint16_t source_y,
                                              uint16_t *dest_x,
                                              uint16_t *dest_y);

#ifdef __cplusplus
}
#endif

#endif /* TINY1C_THERMAL_APP_H */
