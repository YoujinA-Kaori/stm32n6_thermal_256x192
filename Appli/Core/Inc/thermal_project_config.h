/**
  ******************************************************************************
  * @file    thermal_project_config.h
  * @brief   Shared resolution configuration for the Tiny1C thermal pipeline.
  ******************************************************************************
  */

#ifndef THERMAL_PROJECT_CONFIG_H
#define THERMAL_PROJECT_CONFIG_H

#include <stdint.h>

/* Tiny1C native Image + Temperature frame geometry. */
#define CFG_THERMAL_SENSOR_WIDTH                  256U
#define CFG_THERMAL_SENSOR_HEIGHT                 192U
#define CFG_THERMAL_SENSOR_COMBINED_HEIGHT        (CFG_THERMAL_SENSOR_HEIGHT * 2U)

/* Native preview is expanded by the MCU before it is scaled into LVGL areas. */
#define CFG_THERMAL_NATIVE_PREVIEW_SCALE          2U

/* USART1 live preview keeps the established 128x96 / 5 fps bandwidth budget. */
#define CFG_THERMAL_UART_DOWNSAMPLE_STEP_X        2U
#define CFG_THERMAL_UART_DOWNSAMPLE_STEP_Y        2U
#define CFG_THERMAL_UART_FRAME_WIDTH              (CFG_THERMAL_SENSOR_WIDTH / CFG_THERMAL_UART_DOWNSAMPLE_STEP_X)
#define CFG_THERMAL_UART_FRAME_HEIGHT             (CFG_THERMAL_SENSOR_HEIGHT / CFG_THERMAL_UART_DOWNSAMPLE_STEP_Y)

#if ((CFG_THERMAL_SENSOR_WIDTH % CFG_THERMAL_UART_DOWNSAMPLE_STEP_X) != 0U)
#error "Thermal sensor width must be divisible by the UART X downsample step"
#endif

#if ((CFG_THERMAL_SENSOR_HEIGHT % CFG_THERMAL_UART_DOWNSAMPLE_STEP_Y) != 0U)
#error "Thermal sensor height must be divisible by the UART Y downsample step"
#endif

#endif /* THERMAL_PROJECT_CONFIG_H */
