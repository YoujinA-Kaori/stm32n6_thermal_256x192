#ifndef APP_CAMERA_ISP_H
#define APP_CAMERA_ISP_H

/*
 * IMX219 + DCMIPP 自适应参考模块（从 NECCS app_camera.c 抽出）
 * ------------------------------------------------------------
 * 提供三部分自适应能力：
 *   1) ISP 色调/颜色链配置：黑电平扣除 + 逐通道白平衡增益 + gamma。
 *   2) 灰世界自动白平衡（AWB）：只改 DCMIPP 曝光块的 R/G/B 增益，无 I2C。
 *   3) 自动曝光（AE）：按平均亮度伺服，走 曝光行 -> 模拟增益 -> 数字增益
 *      的增益阶梯，通过 IMX219 驱动写寄存器。
 *
 * 与原固件的差异：DCMIPP 句柄和 pipe 由调用方以入参传入（原代码用文件级
 * 静态 hdcmipp），其余算法/常量保持一致，便于移植。
 *
 * 依赖：STM32N6 HAL 的 DCMIPP 驱动、app_camera_imx219.{c,h}。
 */

#include "main.h"            /* 提供 DCMIPP_HandleTypeDef / DCMIPP_PIPEx */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- ISP 色调链常量 ---- */
/* IMX219 数据黑电平基座：10bit 下 64 LSB = ISP 8bit 域的 16 LSB。 */
#define APP_CAMERA_ISP_BLACK_LEVEL      16U

/* ---- AE 伺服常量（亮度尺度 0..187 = 2*R5 + G6 + 2*B5） ---- */
#define APP_CAMERA_AE_TARGET_LUMA       92U      /* 中灰目标 */
#define APP_CAMERA_AE_DEADBAND          10U
#define APP_CAMERA_AE_MIN_EXPOSURE      64U
#define APP_CAMERA_AE_MAX_EXPOSURE      3400U
#define APP_CAMERA_AE_MAX_AGAIN_CODE    200U     /* IMX219 模拟增益 code 上限 */
#define APP_CAMERA_AE_MIN_DGAIN         0x0100U  /* x1.0 */
#define APP_CAMERA_AE_MAX_DGAIN         0x0400U  /* x4.0 */

/* ---- AWB 增益范围（Q7，128 = x1.0） ---- */
#define APP_CAMERA_WB_GAIN_MIN          80U
#define APP_CAMERA_WB_GAIN_MAX          1000U

#define APP_CAMERA_ISP_OK               0
#define APP_CAMERA_ISP_ERROR_CONFIG    -1

/* 8x8 网格统计结果（RGB565 预览缓冲）。 */
typedef struct
{
  uint32_t luma_avg;   /* 0..187 */
  uint32_t r_avg8;     /* 0..255 */
  uint32_t g_avg8;     /* 0..255 */
  uint32_t b_avg8;     /* 0..255 */
  uint32_t sample_count;
} AppCameraISPStats_t;

/* AE/AWB 伺服状态 + 诊断（SWD 可见）。初值即 IMX219 室内典型工作点。 */
extern volatile uint32_t g_app_camera_ae_exposure_lines;
extern volatile uint32_t g_app_camera_ae_again_code;
extern volatile uint32_t g_app_camera_ae_dgain;
extern volatile uint32_t g_app_camera_ae_update_count;
extern volatile uint32_t g_app_camera_wb_gain_r_q7;
extern volatile uint32_t g_app_camera_wb_gain_g_q7;
extern volatile uint32_t g_app_camera_wb_gain_b_q7;
extern volatile uint32_t g_app_camera_wb_update_count;

/*
 * 配置并使能 ISP 色调/颜色链：黑电平标定 -> 白平衡增益 -> gamma。
 * 在 preview(RGB565) 管线里、demosaic 使能之后调用一次。
 * 硬件顺序由 pipe 固定，本函数只做配置+使能。
 */
int32_t AppCameraISP_ConfigureIspChain(DCMIPP_HandleTypeDef *hdcmipp, uint32_t pipe);

/*
 * 把当前 g_app_camera_wb_gain_* 增益推入 DCMIPP 曝光块（demosaic 前，
 * 逐 Bayer 分量）。流式运行时安全（影子寄存器逐帧加载）。
 */
int32_t AppCameraISP_ApplyWhiteBalance(DCMIPP_HandleTypeDef *hdcmipp, uint32_t pipe);

/*
 * 1Hz 自动曝光：按平均亮度做乘法伺服，增益阶梯 曝光->模拟->数字。
 * 会调用 AppCameraIMX219_SetExposure()，必须在允许 I2C 的线程里跑。
 */
void AppCameraISP_RunAutoExposure(uint32_t luma_avg);

/*
 * 1Hz 灰世界白平衡：只改通道增益寄存器，无 I2C。
 * 需要传入 DCMIPP 句柄/pipe 以便回写曝光块增益。
 */
void AppCameraISP_RunAutoWhiteBalance(DCMIPP_HandleTypeDef *hdcmipp, uint32_t pipe,
                                      uint32_t r_avg8, uint32_t g_avg8, uint32_t b_avg8);

/*
 * 从 RGB565 帧缓冲按 8x8 网格采样，算出 luma/r/g/b 平均，喂给 AE/AWB。
 * 调用方需自行保证该内存区域的 D-Cache 一致性（invalidate）。
 */
void AppCameraISP_SampleRgb565(const void *frame_addr, uint32_t width, uint32_t height,
                               uint32_t line_pitch, AppCameraISPStats_t *out);

#ifdef __cplusplus
}
#endif

#endif /* APP_CAMERA_ISP_H */
