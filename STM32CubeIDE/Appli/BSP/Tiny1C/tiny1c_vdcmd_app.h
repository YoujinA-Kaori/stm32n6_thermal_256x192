#ifndef TINY1C_VDCMD_APP_H
#define TINY1C_VDCMD_APP_H

#include <stdint.h>
#include "VDCMD/falcon_cmd.h"

#if VDCMD_ENABLE_TEMP_CALIBRATION
#include "OTHER/temperature.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 Tiny1-C 的 VDCMD 控制通道，当前使用 I2C 方式。
 * @param 无。
 * @return ir_error_t：`IR_SUCCESS` 表示成功。
 * @note 默认使用 all_config.h 中定义的 `VDCMD_I2C_HANDLE`，当前工程为 `hi2c4`。
 */
ir_error_t tiny1c_vdcmd_init(void);

/**
 * @brief 恢复设备默认配置，等价于 `DEF_CFG_ALL`。
 * @param 无。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_restore_all_defaults(void);

/**
 * @brief 读取设备信息。
 * @param id_type 设备信息类型，对应 `enum device_id_types`。
 * @param id_content 输出缓冲区指针，由上层预先分配。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_get_device_info(uint8_t id_type, uint8_t *id_content);

/**
 * @brief 设置预览通道的伪彩模式。
 * @param path 预览通道，取值见 `enum preview_path`。
 * @param color_type 伪彩类型，取值见 `enum pseudo_color_types`。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_set_pseudo_color(enum preview_path path, enum pseudo_color_types color_type);

/**
 * @brief 获取当前伪彩模式。
 * @param path 预览通道。
 * @param color_type 输出伪彩类型指针。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_get_pseudo_color(enum preview_path path, uint8_t *color_type);

/**
 * @brief 启动预览。
 * @param preview_start_param 预览启动参数。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_preview_start(const PreviewStartParam_t *preview_start_param);

/**
 * @brief 停止预览。
 * @param path 预览通道。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_preview_stop(enum preview_path path);

/**
 * @brief 启动 Y16 温度流输出，供上层自行处理温度数据。
 * @param path 预览通道。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_y16_temperature_start(enum preview_path path);

/**
 * @brief 停止 Y16 温度流输出。
 * @param path 预览通道。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_y16_temperature_stop(enum preview_path path);

/**
 * @brief 获取整帧最高温、最低温及其坐标信息。
 * @param max_min_temp_info 输出结构体指针。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_get_frame_max_min_temp(MaxMinTempInfo_t *max_min_temp_info);

/**
 * @brief 获取指定点温度。
 * @param point_pos 目标点坐标。
 * @param point_temp_value 输出温度值指针。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_get_point_temp(IrPoint_t point_pos, uint16_t *point_temp_value);

/**
 * @brief 设置测温距离参数。
 * @param distance_cnt_128_per_meter 距离值，单位为 1/128 米。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_set_distance(uint16_t distance_cnt_128_per_meter);

/**
 * @brief 设置发射率参数。
 * @param emissivity_cnt_128 发射率，单位为 1/128。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_set_emissivity(uint16_t emissivity_cnt_128);

/**
 * @brief Set atmospheric temperature in degrees Celsius.
 * @param atmospheric_temp_c Atmospheric temperature in degrees Celsius.
 * @return ir_error_t Execution result.
 */
ir_error_t tiny1c_vdcmd_set_atmospheric_temp_c(int16_t atmospheric_temp_c);

/**
 * @brief Set reflected temperature in degrees Celsius.
 * @param reflected_temp_c Reflected temperature in degrees Celsius.
 * @return ir_error_t Execution result.
 */
ir_error_t tiny1c_vdcmd_set_reflected_temp_c(int16_t reflected_temp_c);

/**
 * @brief Set atmospheric transmittance in 1/128 units.
 * @param tau_cnt_128 Atmospheric transmittance where 128 means 1.0.
 * @return ir_error_t Execution result.
 */
ir_error_t tiny1c_vdcmd_set_tau(uint16_t tau_cnt_128);

/**
 * @brief Set Tiny1-C gain mode.
 * @param high_gain_enable Non-zero enables high gain, zero enables low gain.
 * @return ir_error_t Execution result.
 */
ir_error_t tiny1c_vdcmd_set_gain_mode(uint8_t high_gain_enable);

/**
 * @brief Enable or disable Tiny1-C automatic shutter/FFC.
 * @param enable Non-zero enables automatic shutter, zero disables it.
 * @return ir_error_t Execution result.
 */
ir_error_t tiny1c_vdcmd_set_auto_shutter_enabled(uint8_t enable);

/**
 * @brief Set automatic shutter minimum interval.
 * @param interval_seconds Interval in seconds.
 * @return ir_error_t Execution result.
 */
ir_error_t tiny1c_vdcmd_set_auto_shutter_min_interval(uint16_t interval_seconds);

/**
 * @brief Set automatic shutter maximum interval.
 * @param interval_seconds Interval in seconds.
 * @return ir_error_t Execution result.
 */
ir_error_t tiny1c_vdcmd_set_auto_shutter_max_interval(uint16_t interval_seconds);

/**
 * @brief Set automatic shutter temperature threshold.
 * @param threshold_cnt Threshold in module counts. 36 counts are about 1 degree Celsius.
 * @return ir_error_t Execution result.
 */
ir_error_t tiny1c_vdcmd_set_auto_shutter_temp_threshold(uint16_t threshold_cnt);

/**
 * @brief Trigger one manual FFC/B-update.
 * @param None
 * @return ir_error_t Execution result.
 */
ir_error_t tiny1c_vdcmd_trigger_ffc(void);

#ifdef __cplusplus
}
#endif

#endif /* TINY1C_VDCMD_APP_H */
