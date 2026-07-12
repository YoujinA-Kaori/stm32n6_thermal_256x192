#include "app_camera_isp.h"

#include "app_camera_imx219.h"

/* AE/AWB 伺服状态 + 诊断。初值 = IMX219 室内典型工作点：
 * 曝光 0x0D00 行、模拟增益 code 0x80、数字增益 x1，WB R x1.35 / B x1.55。 */
volatile uint32_t g_app_camera_ae_exposure_lines = 0x0D00U;
volatile uint32_t g_app_camera_ae_again_code = 0x80U;
volatile uint32_t g_app_camera_ae_dgain = 0x0100U;
volatile uint32_t g_app_camera_ae_update_count = 0U;
volatile uint32_t g_app_camera_wb_gain_r_q7 = 173U;   /* x1.35 */
volatile uint32_t g_app_camera_wb_gain_g_q7 = 128U;   /* x1.00 */
volatile uint32_t g_app_camera_wb_gain_b_q7 = 198U;   /* x1.55 */
volatile uint32_t g_app_camera_wb_update_count = 0U;

/* 采样网格：8x8 稀疏格点。 */
#define APP_CAMERA_ISP_SAMPLE_GRID   8U

static uint32_t AppCameraISP_Rgb565Luma(uint32_t pixel)
{
  uint32_t r = (pixel >> 11) & 0x1FU;
  uint32_t g = (pixel >> 5) & 0x3FU;
  uint32_t b = pixel & 0x1FU;

  return (r * 2U) + g + (b * 2U);
}

/* Q7 增益(128 = x1.0)转成 DCMIPP 曝光块的 shift+multiplier 编码
 * (gain = multiplier * 2^shift / 128)，与 ST ISP 库转换一致。 */
static void AppCameraISP_GainToShiftMult(uint32_t gain_q7, uint8_t *shift, uint8_t *mult)
{
  uint32_t value = gain_q7;
  uint8_t shift_count = 0U;

  while ((value >= 256U) && (shift_count < 7U))
  {
    value >>= 1;
    shift_count++;
  }
  if (value > 255U)
  {
    value = 255U;
  }

  *shift = shift_count;
  *mult = (uint8_t)value;
}

int32_t AppCameraISP_ApplyWhiteBalance(DCMIPP_HandleTypeDef *hdcmipp, uint32_t pipe)
{
  DCMIPP_ExposureConfTypeDef exposure_config = {0};
  HAL_StatusTypeDef hal_status;

  if (hdcmipp == 0)
  {
    return APP_CAMERA_ISP_ERROR_CONFIG;
  }

  AppCameraISP_GainToShiftMult(g_app_camera_wb_gain_r_q7,
                               &exposure_config.ShiftRed, &exposure_config.MultiplierRed);
  AppCameraISP_GainToShiftMult(g_app_camera_wb_gain_g_q7,
                               &exposure_config.ShiftGreen, &exposure_config.MultiplierGreen);
  AppCameraISP_GainToShiftMult(g_app_camera_wb_gain_b_q7,
                               &exposure_config.ShiftBlue, &exposure_config.MultiplierBlue);

  hal_status = HAL_DCMIPP_PIPE_SetISPExposureConfig(hdcmipp, pipe, &exposure_config);
  if (hal_status == HAL_OK)
  {
    hal_status = HAL_DCMIPP_PIPE_EnableISPExposure(hdcmipp, pipe);
  }

  return (hal_status == HAL_OK) ? APP_CAMERA_ISP_OK : APP_CAMERA_ISP_ERROR_CONFIG;
}

int32_t AppCameraISP_ConfigureIspChain(DCMIPP_HandleTypeDef *hdcmipp, uint32_t pipe)
{
  DCMIPP_BlackLevelConfTypeDef black_level_config;
  HAL_StatusTypeDef hal_status;

  if (hdcmipp == 0)
  {
    return APP_CAMERA_ISP_ERROR_CONFIG;
  }

  /* 最小 ISP 链(修灰/发白)：raw 基座黑电平扣除、demosaic 前逐通道白平衡增益、
   * RGB 输出的硬件 gamma 曲线。硬件顺序由 pipe 固定，这里只配置+使能。 */
  black_level_config.RedCompBlackLevel = (uint8_t)APP_CAMERA_ISP_BLACK_LEVEL;
  black_level_config.GreenCompBlackLevel = (uint8_t)APP_CAMERA_ISP_BLACK_LEVEL;
  black_level_config.BlueCompBlackLevel = (uint8_t)APP_CAMERA_ISP_BLACK_LEVEL;

  hal_status = HAL_DCMIPP_PIPE_SetISPBlackLevelCalibrationConfig(hdcmipp, pipe,
                                                                 &black_level_config);
  if (hal_status == HAL_OK)
  {
    hal_status = HAL_DCMIPP_PIPE_EnableISPBlackLevelCalibration(hdcmipp, pipe);
  }
  if (hal_status != HAL_OK)
  {
    return APP_CAMERA_ISP_ERROR_CONFIG;
  }

  if (AppCameraISP_ApplyWhiteBalance(hdcmipp, pipe) != APP_CAMERA_ISP_OK)
  {
    return APP_CAMERA_ISP_ERROR_CONFIG;
  }

  hal_status = HAL_DCMIPP_PIPE_EnableGammaConversion(hdcmipp, pipe);
  if (hal_status != HAL_OK)
  {
    return APP_CAMERA_ISP_ERROR_CONFIG;
  }

  return APP_CAMERA_ISP_OK;
}

void AppCameraISP_RunAutoExposure(uint32_t luma_avg)
{
  uint32_t ratio_q8;
  uint64_t total;
  uint32_t exposure;
  uint32_t again_x256;
  uint32_t dgain;

  if (luma_avg < 1U)
  {
    luma_avg = 1U;
  }
  if ((luma_avg + APP_CAMERA_AE_DEADBAND >= APP_CAMERA_AE_TARGET_LUMA) &&
      (luma_avg <= APP_CAMERA_AE_TARGET_LUMA + APP_CAMERA_AE_DEADBAND))
  {
    return;
  }

  /* 单步修正比，钳到 +/-35%，在 1Hz 环上收敛而不振荡。 */
  ratio_q8 = (APP_CAMERA_AE_TARGET_LUMA * 256U) / luma_avg;
  if (ratio_q8 > 345U)
  {
    ratio_q8 = 345U;
  }
  if (ratio_q8 < 166U)
  {
    ratio_q8 = 166U;
  }

  /* 当前总增益 = 曝光行 * 模拟 * 数字(增益项各 Q8)。模拟 code c 的增益 256/(256-c)。 */
  again_x256 = (256U * 256U) / (256U - g_app_camera_ae_again_code);
  dgain = g_app_camera_ae_dgain;
  total = (uint64_t)g_app_camera_ae_exposure_lines * again_x256 * dgain;
  total = (total * ratio_q8) >> 8;

  /* 沿阶梯重新分配：先曝光(无噪声代价)，再模拟增益，最后数字增益。 */
  exposure = (uint32_t)(total / ((uint64_t)256U * 256U));
  again_x256 = 256U;
  dgain = 256U;
  if (exposure > APP_CAMERA_AE_MAX_EXPOSURE)
  {
    uint64_t remainder = total / APP_CAMERA_AE_MAX_EXPOSURE;

    exposure = APP_CAMERA_AE_MAX_EXPOSURE;
    again_x256 = (uint32_t)(remainder / 256U);
    if (again_x256 < 256U)
    {
      again_x256 = 256U;
    }
    {
      const uint32_t max_again_x256 =
          (256U * 256U) / (256U - APP_CAMERA_AE_MAX_AGAIN_CODE);

      if (again_x256 > max_again_x256)
      {
        /* remainder 是 Q16(模拟 x 数字)；除以 Q8 模拟上限得到 Q8 数字增益。 */
        dgain = (uint32_t)(remainder / max_again_x256);
        again_x256 = max_again_x256;
        if (dgain < APP_CAMERA_AE_MIN_DGAIN)
        {
          dgain = APP_CAMERA_AE_MIN_DGAIN;
        }
        if (dgain > APP_CAMERA_AE_MAX_DGAIN)
        {
          dgain = APP_CAMERA_AE_MAX_DGAIN;
        }
      }
    }
  }
  else if (exposure < APP_CAMERA_AE_MIN_EXPOSURE)
  {
    exposure = APP_CAMERA_AE_MIN_EXPOSURE;
  }

  {
    /* 模拟增益因子换回 IMX219 code c = 256 - 256/g。 */
    uint32_t again_code = 256U - ((256U * 256U) / again_x256);

    if (again_code > APP_CAMERA_AE_MAX_AGAIN_CODE)
    {
      again_code = APP_CAMERA_AE_MAX_AGAIN_CODE;
    }

    if ((exposure == g_app_camera_ae_exposure_lines) &&
        (again_code == g_app_camera_ae_again_code) &&
        (dgain == g_app_camera_ae_dgain))
    {
      return;
    }

    if (AppCameraIMX219_SetExposure((uint16_t)exposure,
                                    (uint8_t)again_code,
                                    (uint16_t)dgain) == APP_CAMERA_IMX219_OK)
    {
      g_app_camera_ae_exposure_lines = exposure;
      g_app_camera_ae_again_code = again_code;
      g_app_camera_ae_dgain = dgain;
      g_app_camera_ae_update_count++;
    }
  }
}

void AppCameraISP_RunAutoWhiteBalance(DCMIPP_HandleTypeDef *hdcmipp, uint32_t pipe,
                                      uint32_t r_avg8, uint32_t g_avg8, uint32_t b_avg8)
{
  uint32_t target_r;
  uint32_t target_b;
  uint32_t new_r;
  uint32_t new_b;

  if ((r_avg8 < 4U) || (g_avg8 < 4U) || (b_avg8 < 4U))
  {
    return; /* 太暗，比值无意义。 */
  }

  /* 使各通道均值相等的理想增益，然后每次 IIR 走 1/4 步长。 */
  target_r = (g_app_camera_wb_gain_r_q7 * g_avg8) / r_avg8;
  target_b = (g_app_camera_wb_gain_b_q7 * g_avg8) / b_avg8;
  new_r = g_app_camera_wb_gain_r_q7 + ((int32_t)(target_r - g_app_camera_wb_gain_r_q7) / 4);
  new_b = g_app_camera_wb_gain_b_q7 + ((int32_t)(target_b - g_app_camera_wb_gain_b_q7) / 4);

  if (new_r < APP_CAMERA_WB_GAIN_MIN)
  {
    new_r = APP_CAMERA_WB_GAIN_MIN;
  }
  if (new_r > APP_CAMERA_WB_GAIN_MAX)
  {
    new_r = APP_CAMERA_WB_GAIN_MAX;
  }
  if (new_b < APP_CAMERA_WB_GAIN_MIN)
  {
    new_b = APP_CAMERA_WB_GAIN_MIN;
  }
  if (new_b > APP_CAMERA_WB_GAIN_MAX)
  {
    new_b = APP_CAMERA_WB_GAIN_MAX;
  }

  /* 死区：小于 2% 的移动不动寄存器。 */
  if (((new_r > g_app_camera_wb_gain_r_q7) ? (new_r - g_app_camera_wb_gain_r_q7)
                                           : (g_app_camera_wb_gain_r_q7 - new_r)) < 3U &&
      ((new_b > g_app_camera_wb_gain_b_q7) ? (new_b - g_app_camera_wb_gain_b_q7)
                                           : (g_app_camera_wb_gain_b_q7 - new_b)) < 3U)
  {
    return;
  }

  g_app_camera_wb_gain_r_q7 = new_r;
  g_app_camera_wb_gain_b_q7 = new_b;
  if (AppCameraISP_ApplyWhiteBalance(hdcmipp, pipe) == APP_CAMERA_ISP_OK)
  {
    g_app_camera_wb_update_count++;
  }
}

void AppCameraISP_SampleRgb565(const void *frame_addr, uint32_t width, uint32_t height,
                               uint32_t line_pitch, AppCameraISPStats_t *out)
{
  uint32_t sample_count = 0U;
  uint32_t luma_sum = 0U;
  uint32_t r_sum = 0U;
  uint32_t g_sum = 0U;
  uint32_t b_sum = 0U;

  if ((out == 0) || (frame_addr == 0) || (line_pitch == 0U) ||
      (width < 2U) || (height < 2U))
  {
    return;
  }

  for (uint32_t row = 0U; row < APP_CAMERA_ISP_SAMPLE_GRID; row++)
  {
    uint32_t y = (row * (height - 1U)) / (APP_CAMERA_ISP_SAMPLE_GRID - 1U);
    const uint16_t *row_pixels =
        (const uint16_t *)(const void *)((const uint8_t *)frame_addr + (y * line_pitch));

    for (uint32_t col = 0U; col < APP_CAMERA_ISP_SAMPLE_GRID; col++)
    {
      uint32_t x = (col * (width - 1U)) / (APP_CAMERA_ISP_SAMPLE_GRID - 1U);
      uint32_t pixel = row_pixels[x];

      luma_sum += AppCameraISP_Rgb565Luma(pixel);
      r_sum += (pixel >> 11) & 0x1FU;
      g_sum += (pixel >> 5) & 0x3FU;
      b_sum += pixel & 0x1FU;
      sample_count++;
    }
  }

  if (sample_count == 0U)
  {
    return;
  }

  out->sample_count = sample_count;
  out->luma_avg = luma_sum / sample_count;
  /* 5/6bit 通道均值扩展到 8bit(复制高位补低位)。 */
  out->r_avg8 = ((r_sum / sample_count) << 3) | ((r_sum / sample_count) >> 2);
  out->g_avg8 = ((g_sum / sample_count) << 2) | ((g_sum / sample_count) >> 4);
  out->b_avg8 = ((b_sum / sample_count) << 3) | ((b_sum / sample_count) >> 2);
}
