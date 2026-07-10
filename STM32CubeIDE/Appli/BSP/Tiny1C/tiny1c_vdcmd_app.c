#include "tiny1c_vdcmd_app.h"

#include <stddef.h>
#include <string.h>

#include "i2c.h"
#include "tx_api.h"

#define CFG_TINY1C_VDCMD_TEMP_MIN_C          (-43)
#define CFG_TINY1C_VDCMD_TEMP_MAX_C          627
#define CFG_TINY1C_VDCMD_DISTANCE_MAX_CM     20000U
#define CFG_TINY1C_VDCMD_TAU_MIN_CNT_128     1U
#define CFG_TINY1C_VDCMD_TAU_MAX_CNT_128     128U

static TX_MUTEX g_tiny1c_vdcmd_mutex;
static volatile uint8_t g_tiny1c_vdcmd_mutex_created = 0U;

/**
 * @brief Ensure the Tiny1-C VDCMD mutex exists and acquire it.
 * @return ir_error_t `IR_SUCCESS` when the mutex is held.
 */
static ir_error_t tiny1c_vdcmd_lock(void)
{
    if (g_tiny1c_vdcmd_mutex_created == 0U)
    {
        if (tx_mutex_create(&g_tiny1c_vdcmd_mutex, "tiny1c_vdcmd_mutex", TX_INHERIT) != TX_SUCCESS)
        {
            return IR_MEM_ALLOC_FAIL;
        }
        g_tiny1c_vdcmd_mutex_created = 1U;
    }

    if (tx_mutex_get(&g_tiny1c_vdcmd_mutex, TX_WAIT_FOREVER) != TX_SUCCESS)
    {
        return IR_MEM_ALLOC_FAIL;
    }

    if (app_i2c4_bus_lock() != TX_SUCCESS)
    {
        (void)tx_mutex_put(&g_tiny1c_vdcmd_mutex);
        return IR_MEM_ALLOC_FAIL;
    }

    return IR_SUCCESS;
}

/**
 * @brief Release the Tiny1-C VDCMD mutex when it is held.
 * @return None.
 */
static void tiny1c_vdcmd_unlock(void)
{
    app_i2c4_bus_unlock();

    if (g_tiny1c_vdcmd_mutex_created != 0U)
    {
        (void)tx_mutex_put(&g_tiny1c_vdcmd_mutex);
    }
}

/**
 * @brief Convert Celsius temperature to Tiny1-C TPD parameter units.
 * @param temp_c Temperature in degrees Celsius.
 * @return uint16_t Temperature parameter in Kelvin units.
 */
static uint16_t tiny1c_vdcmd_temp_c_to_param(int16_t temp_c)
{
    int32_t temp_clamped = temp_c;

    if (temp_clamped < CFG_TINY1C_VDCMD_TEMP_MIN_C)
    {
        temp_clamped = CFG_TINY1C_VDCMD_TEMP_MIN_C;
    }
    else if (temp_clamped > CFG_TINY1C_VDCMD_TEMP_MAX_C)
    {
        temp_clamped = CFG_TINY1C_VDCMD_TEMP_MAX_C;
    }

    return (uint16_t)(temp_clamped + 273);
}

/**
 * @brief Convert atmospheric transmittance to Tiny1-C parameter units.
 * @param tau_cnt_128 Atmospheric transmittance in 1/128 units.
 * @return uint16_t Clamped transmittance parameter.
 */
static uint16_t tiny1c_vdcmd_tau_to_param(uint16_t tau_cnt_128)
{
    if (tau_cnt_128 < CFG_TINY1C_VDCMD_TAU_MIN_CNT_128)
    {
        return CFG_TINY1C_VDCMD_TAU_MIN_CNT_128;
    }

    if (tau_cnt_128 > CFG_TINY1C_VDCMD_TAU_MAX_CNT_128)
    {
        return CFG_TINY1C_VDCMD_TAU_MAX_CNT_128;
    }

    return tau_cnt_128;
}

/**
 * @brief 初始化 VDCMD 并注册 I2C 读写通道。
 * @param 无。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_init(void)
{
    ir_error_t rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = vdcmd_init_by_type(VDCMD_I2C_VDCMD);
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief 恢复设备默认配置。
 * @param 无。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_restore_all_defaults(void)
{
    ir_error_t rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = restore_default_cfg(DEF_CFG_ALL);
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief 获取设备信息。
 * @param id_type 设备信息类型。
 * @param id_content 输出缓冲区指针。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_get_device_info(uint8_t id_type, uint8_t *id_content)
{
    ir_error_t rst;

    if (id_content == NULL)
    {
        return IR_ERROR_PARAM;
    }

    rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = get_device_info((enum device_id_types)id_type, id_content);
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief 设置伪彩模式。
 * @param path 预览通道。
 * @param color_type 伪彩类型。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_set_pseudo_color(enum preview_path path, enum pseudo_color_types color_type)
{
    ir_error_t rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = pseudo_color_set(path, color_type);
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief 读取当前伪彩模式。
 * @param path 预览通道。
 * @param color_type 输出伪彩类型指针。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_get_pseudo_color(enum preview_path path, uint8_t *color_type)
{
    ir_error_t rst;

    if (color_type == NULL)
    {
        return IR_ERROR_PARAM;
    }

    rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = pseudo_color_get(path, color_type);
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief 启动预览。
 * @param preview_start_param 预览参数。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_preview_start(const PreviewStartParam_t *preview_start_param)
{
    PreviewStartParam_t local_param;
    ir_error_t rst;

    if (preview_start_param == NULL)
    {
        return IR_ERROR_PARAM;
    }

    /* 复制到本地可写副本，避免底层接口修改上层只读数据。 */
    memcpy(&local_param, preview_start_param, sizeof(local_param));

    rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = preview_start(&local_param);
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief 停止预览。
 * @param path 预览通道。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_preview_stop(enum preview_path path)
{
    ir_error_t rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = preview_stop(path);
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief 启动 Y16 温度流输出。
 * @param path 预览通道。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_y16_temperature_start(enum preview_path path)
{
    ir_error_t rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = y16_preview_start(path, Y16_MODE_TEMPERATURE);
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief 停止 Y16 温度流输出。
 * @param path 预览通道。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_y16_temperature_stop(enum preview_path path)
{
    ir_error_t rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = y16_preview_stop(path);
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief 获取整帧最高温和最低温信息。
 * @param max_min_temp_info 输出结构体指针。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_get_frame_max_min_temp(MaxMinTempInfo_t *max_min_temp_info)
{
    ir_error_t rst;

    if (max_min_temp_info == NULL)
    {
        return IR_ERROR_PARAM;
    }

    rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = tpd_get_max_min_temp_info(max_min_temp_info);
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief 获取指定点温度。
 * @param point_pos 点坐标。
 * @param point_temp_value 输出温度值指针。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_get_point_temp(IrPoint_t point_pos, uint16_t *point_temp_value)
{
    ir_error_t rst;

    if (point_temp_value == NULL)
    {
        return IR_ERROR_PARAM;
    }

    rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = tpd_get_point_temp_info(point_pos, point_temp_value);
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief 设置测温距离参数。
 * @param distance_cnt_128_per_meter 距离值，单位为 1/128 米。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_set_distance(uint16_t distance_cnt_128_per_meter)
{
    ir_error_t rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = set_prop_tpd_params(TPD_PROP_DISTANCE, distance_cnt_128_per_meter);
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief 设置发射率参数。
 * @param emissivity_cnt_128 发射率，单位为 1/128。
 * @return ir_error_t：执行结果。
 */
ir_error_t tiny1c_vdcmd_set_emissivity(uint16_t emissivity_cnt_128)
{
    ir_error_t rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = set_prop_tpd_params(TPD_PROP_EMS, emissivity_cnt_128);
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief Set atmospheric temperature in degrees Celsius.
 * @param atmospheric_temp_c Atmospheric temperature in degrees Celsius.
 * @return ir_error_t Execution result.
 */
ir_error_t tiny1c_vdcmd_set_atmospheric_temp_c(int16_t atmospheric_temp_c)
{
    ir_error_t rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = set_prop_tpd_params(TPD_PROP_TA, tiny1c_vdcmd_temp_c_to_param(atmospheric_temp_c));
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief Set reflected temperature in degrees Celsius.
 * @param reflected_temp_c Reflected temperature in degrees Celsius.
 * @return ir_error_t Execution result.
 */
ir_error_t tiny1c_vdcmd_set_reflected_temp_c(int16_t reflected_temp_c)
{
    ir_error_t rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = set_prop_tpd_params(TPD_PROP_TU, tiny1c_vdcmd_temp_c_to_param(reflected_temp_c));
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief Set atmospheric transmittance in 1/128 units.
 * @param tau_cnt_128 Atmospheric transmittance where 128 means 1.0.
 * @return ir_error_t Execution result.
 */
ir_error_t tiny1c_vdcmd_set_tau(uint16_t tau_cnt_128)
{
    ir_error_t rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = set_prop_tpd_params(TPD_PROP_TAU, tiny1c_vdcmd_tau_to_param(tau_cnt_128));
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief Set Tiny1-C gain mode.
 * @param high_gain_enable Non-zero enables high gain, zero enables low gain.
 * @return ir_error_t Execution result.
 */
ir_error_t tiny1c_vdcmd_set_gain_mode(uint8_t high_gain_enable)
{
    ir_error_t rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = set_prop_tpd_params(TPD_PROP_GAIN_SEL, (high_gain_enable != 0U) ? 1U : 0U);
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief Enable or disable Tiny1-C automatic shutter/FFC.
 * @param enable Non-zero enables automatic shutter, zero disables it.
 * @return ir_error_t Execution result.
 */
ir_error_t tiny1c_vdcmd_set_auto_shutter_enabled(uint8_t enable)
{
    ir_error_t rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = set_prop_auto_shutter_params(SHUTTER_PROP_SWITCH, (enable != 0U) ? 1U : 0U);
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief Set automatic shutter minimum interval.
 * @param interval_seconds Interval in seconds.
 * @return ir_error_t Execution result.
 */
ir_error_t tiny1c_vdcmd_set_auto_shutter_min_interval(uint16_t interval_seconds)
{
    ir_error_t rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = set_prop_auto_shutter_params(SHUTTER_PROP_MIN_INTERVAL, interval_seconds);
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief Set automatic shutter maximum interval.
 * @param interval_seconds Interval in seconds.
 * @return ir_error_t Execution result.
 */
ir_error_t tiny1c_vdcmd_set_auto_shutter_max_interval(uint16_t interval_seconds)
{
    ir_error_t rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = set_prop_auto_shutter_params(SHUTTER_PROP_MAX_INTERVAL, interval_seconds);
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief Set automatic shutter temperature threshold.
 * @param threshold_cnt Threshold in module counts. 36 counts are about 1 degree Celsius.
 * @return ir_error_t Execution result.
 */
ir_error_t tiny1c_vdcmd_set_auto_shutter_temp_threshold(uint16_t threshold_cnt)
{
    ir_error_t rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = set_prop_auto_shutter_params(SHUTTER_PROP_TEMP_THRESHOLD_B, threshold_cnt);
    tiny1c_vdcmd_unlock();
    return rst;
}

/**
 * @brief Trigger one manual FFC/B-update.
 * @param None
 * @return ir_error_t Execution result.
 */
ir_error_t tiny1c_vdcmd_trigger_ffc(void)
{
    ir_error_t rst = tiny1c_vdcmd_lock();
    if (rst != IR_SUCCESS)
    {
        return rst;
    }

    rst = ooc_b_update(B_UPDATE);
    tiny1c_vdcmd_unlock();
    return rst;
}
