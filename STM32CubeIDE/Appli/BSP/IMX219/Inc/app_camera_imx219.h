#ifndef APP_CAMERA_IMX219_H
#define APP_CAMERA_IMX219_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define APP_CAMERA_IMX219_OK                 0
#define APP_CAMERA_IMX219_ERROR_INVALID_ARG -1
#define APP_CAMERA_IMX219_ERROR_I2C_LOCK    -2
#define APP_CAMERA_IMX219_ERROR_I2C         -3
#define APP_CAMERA_IMX219_ERROR_CHIP_ID     -4

extern volatile uint32_t g_app_camera_imx219_last_reg;
extern volatile uint32_t g_app_camera_imx219_last_error;
extern volatile uint32_t g_app_camera_imx219_last_hal_status;
extern volatile uint32_t g_app_camera_imx219_transfer_count;
extern volatile uint32_t g_app_camera_imx219_readback_status;
extern volatile uint32_t g_app_camera_imx219_readback_lane_mode;
extern volatile uint32_t g_app_camera_imx219_readback_data_format0;
extern volatile uint32_t g_app_camera_imx219_readback_data_format1;
extern volatile uint32_t g_app_camera_imx219_readback_test_pattern;
extern volatile uint32_t g_app_camera_imx219_readback_stream;
extern volatile uint32_t g_app_camera_imx219_power_cycle_count;
extern volatile uint32_t g_app_camera_imx219_reset_retry_count;
extern volatile uint32_t g_app_camera_imx219_scan_count;
extern volatile uint32_t g_app_camera_imx219_scan_first_ack;
extern volatile uint32_t g_app_camera_imx219_scan_ack_mask[4];
extern volatile uint32_t g_app_camera_imx219_readback_exck_freq;
extern volatile uint32_t g_app_camera_imx219_readback_line_length;
extern volatile uint32_t g_app_camera_imx219_readback_frame_length;
extern volatile uint32_t g_app_camera_imx219_readback_exposure;
extern volatile uint32_t g_app_camera_imx219_readback_analog_gain;
extern volatile uint32_t g_app_camera_imx219_readback_digital_gain;
extern volatile uint32_t g_app_camera_imx219_readback_x_start;
extern volatile uint32_t g_app_camera_imx219_readback_x_end;
extern volatile uint32_t g_app_camera_imx219_readback_y_start;
extern volatile uint32_t g_app_camera_imx219_readback_y_end;
extern volatile uint32_t g_app_camera_imx219_readback_x_output;
extern volatile uint32_t g_app_camera_imx219_readback_y_output;
extern volatile uint32_t g_app_camera_imx219_readback_binning_h;
extern volatile uint32_t g_app_camera_imx219_readback_binning_v;
extern volatile uint32_t g_app_camera_imx219_readback_pll_vt_mpy;
extern volatile uint32_t g_app_camera_imx219_readback_pll_op_mpy;

typedef struct
{
  uint16_t width;
  uint16_t height;
  uint16_t fps;
  uint32_t input_clock_hz;
} AppCameraIMX219Config_t;

int32_t AppCameraIMX219_Init(const AppCameraIMX219Config_t *config, uint16_t *chip_id);
int32_t AppCameraIMX219_SetStream(uint8_t enable);
int32_t AppCameraIMX219_SetTestPattern(uint8_t enable);
int32_t AppCameraIMX219_UpdateDiagnostics(void);
/* Grouped exposure update for the AE loop: integration lines, analog gain
 * code (0..232, gain = 256/(256-code)) and digital gain (0x0100 = x1). */
int32_t AppCameraIMX219_SetExposure(uint16_t exposure_lines,
                                    uint8_t analog_gain_code,
                                    uint16_t digital_gain_code);

#ifdef __cplusplus
}
#endif

#endif /* APP_CAMERA_IMX219_H */
