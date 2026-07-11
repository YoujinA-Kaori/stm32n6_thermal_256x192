/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#include "events_init.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "lvgl.h"
#include "custom.h"
#include "app_threadx.h"
#include "Tiny1C/tiny1c_thermal_app.h"
#include "Tiny1C/tiny1c_vdcmd_app.h"
#include "RGBLCD/rgblcd.h"

extern lv_obj_t *g_contrast_value_label;

#define CFG_THERMAL_GUI_PSEUDO_COUNT 10U
#define CFG_THERMAL_GUI_PREVIEW_BASE_MIRROR_ENABLE 1U
#define CFG_THERMAL_GUI_PREVIEW_BASE_FLIP_ENABLE   1U

typedef struct
{
    lv_coord_t x;
    lv_coord_t y;
    enum pseudo_color_types mode;
    const char *name;
} thermal_gui_pseudo_map_t;

static const thermal_gui_pseudo_map_t g_pseudo_map[CFG_THERMAL_GUI_PSEUDO_COUNT] =
{
    {  8,  46, PSEUDO_COLOR_MODE_1,  "白热" },
    {  8,  94, PSEUDO_COLOR_MODE_3,  "热像" },
    { 140, 238, PSEUDO_COLOR_MODE_4, "铁红" },
    {  8, 142, PSEUDO_COLOR_MODE_5,  "锐化" },
    { 140,  94, PSEUDO_COLOR_MODE_6, "绿热" },
    { 140, 142, PSEUDO_COLOR_MODE_7, "户外" },
    {  8, 190, PSEUDO_COLOR_MODE_8,  "夜视" },
    { 140, 190, PSEUDO_COLOR_MODE_9, "医疗" },
    {  8, 238, PSEUDO_COLOR_MODE_10, "测试" },
    { 140,  46, PSEUDO_COLOR_MODE_11, "黑热" },
};

static uint8_t g_current_pseudo_index = 3U;
static uint8_t g_current_gain_high = 1U;
static uint8_t g_current_auto_ffc = 1U;
static uint8_t g_current_ai_detect_enable = 0U;
static uint8_t g_current_unit_celsius = 1U;
static uint8_t g_current_contrast_slider = 54U;
static uint8_t g_current_mirror_enable = 0U;
static uint8_t g_current_flip_enable = 0U;
static uint8_t g_current_center_cross_enable = 1U;
static uint8_t g_current_center_temp_enable = 1U;
static uint8_t g_current_hi_lo_mark_enable = 1U;
static uint8_t g_config_saved = 0U;
static uint8_t g_preview_fullscreen_active = 0U;

/**
 * @brief Apply the effective preview transform on top of the new default orientation.
 * @return None.
 */
static void thermal_gui_apply_preview_transform(void)
{
    tiny1c_thermal_app_set_preview_transform((uint8_t)(CFG_THERMAL_GUI_PREVIEW_BASE_MIRROR_ENABLE ^ g_current_mirror_enable),
                                             (uint8_t)(CFG_THERMAL_GUI_PREVIEW_BASE_FLIP_ENABLE ^ g_current_flip_enable));
}

/**
 * @brief Set a badge label text.
 * @param badge Badge container object.
 * @param text New text content.
 * @return None.
 */
static void thermal_gui_set_badge_text(lv_obj_t *badge, const char *text)
{
    lv_obj_t *label;

    if ((badge == NULL) || (text == NULL))
    {
        return;
    }

    label = lv_obj_get_child(badge, 0);
    if (label != NULL)
    {
        lv_label_set_text(label, text);
    }
}

/**
 * @brief Update a button's selected style.
 * @param button Target button.
 * @param selected True for active state.
 * @param accent Accent color.
 * @return None.
 */
static void thermal_gui_set_button_selected(lv_obj_t *button, bool selected, lv_color_t accent)
{
    if (button == NULL)
    {
        return;
    }

    lv_obj_set_style_border_width(button, selected ? 2 : 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(button, selected ? accent : lv_color_hex(0x26313b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, selected ? lv_color_hex(0x24201b) : lv_color_hex(0x11171d), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(button, selected ? accent : lv_color_hex(0xf1f5f7), LV_PART_MAIN | LV_STATE_DEFAULT);
}

/**
 * @brief Apply preview overlay visibility according to the current switch states.
 * @param ui UI context.
 * @return None.
 */
static void thermal_gui_apply_preview_overlay_state(lv_ui *ui)
{
    if (ui == NULL)
    {
        return;
    }

    if (g_current_mirror_enable != 0U)
    {
        lv_obj_add_state(ui->WidgetsDemo_sw_mirror, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_clear_state(ui->WidgetsDemo_sw_mirror, LV_STATE_CHECKED);
    }

    if (g_current_flip_enable != 0U)
    {
        lv_obj_add_state(ui->WidgetsDemo_sw_flip, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_clear_state(ui->WidgetsDemo_sw_flip, LV_STATE_CHECKED);
    }

    if (g_current_center_cross_enable != 0U)
    {
        lv_obj_add_state(ui->WidgetsDemo_sw_center_cross, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_clear_state(ui->WidgetsDemo_sw_center_cross, LV_STATE_CHECKED);
    }

    if (g_current_center_temp_enable != 0U)
    {
        lv_obj_add_state(ui->WidgetsDemo_sw_center_temp, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_clear_state(ui->WidgetsDemo_sw_center_temp, LV_STATE_CHECKED);
    }

    if (g_current_hi_lo_mark_enable != 0U)
    {
        lv_obj_add_state(ui->WidgetsDemo_sw_hi_lo_mark, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_clear_state(ui->WidgetsDemo_sw_hi_lo_mark, LV_STATE_CHECKED);
    }

    if (g_current_ai_detect_enable != 0U)
    {
        lv_obj_add_state(ui->WidgetsDemo_sw_ai_detect, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_clear_state(ui->WidgetsDemo_sw_ai_detect, LV_STATE_CHECKED);
    }

    if (g_current_center_cross_enable != 0U)
    {
        lv_obj_clear_flag(ui->WidgetsDemo_preview_cross_h, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui->WidgetsDemo_preview_cross_v, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui->WidgetsDemo_fullscreen_preview_cross_h, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui->WidgetsDemo_fullscreen_preview_cross_v, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(ui->WidgetsDemo_preview_cross_h, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->WidgetsDemo_preview_cross_v, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->WidgetsDemo_fullscreen_preview_cross_h, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->WidgetsDemo_fullscreen_preview_cross_v, LV_OBJ_FLAG_HIDDEN);
    }

    if (g_current_center_temp_enable != 0U)
    {
        lv_obj_clear_flag(ui->WidgetsDemo_preview_center_temp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui->WidgetsDemo_fullscreen_preview_center_temp, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(ui->WidgetsDemo_preview_center_temp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->WidgetsDemo_fullscreen_preview_center_temp, LV_OBJ_FLAG_HIDDEN);
    }

    if (g_current_hi_lo_mark_enable != 0U)
    {
        lv_obj_clear_flag(ui->WidgetsDemo_preview_max_temp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui->WidgetsDemo_preview_min_temp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui->WidgetsDemo_preview_max_marker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui->WidgetsDemo_preview_min_marker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui->WidgetsDemo_fullscreen_preview_max_temp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui->WidgetsDemo_fullscreen_preview_min_temp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui->WidgetsDemo_fullscreen_preview_max_marker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui->WidgetsDemo_fullscreen_preview_min_marker, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(ui->WidgetsDemo_preview_max_temp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->WidgetsDemo_preview_min_temp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->WidgetsDemo_preview_max_marker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->WidgetsDemo_preview_min_marker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->WidgetsDemo_fullscreen_preview_max_temp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->WidgetsDemo_fullscreen_preview_min_temp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->WidgetsDemo_fullscreen_preview_max_marker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui->WidgetsDemo_fullscreen_preview_min_marker, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Apply the preview frame geometry for normal or full-screen mode.
 * @param ui UI context.
 * @param fullscreen_enable Non-zero to enable full-screen layout.
 * @return None.
 */
/**
 * @brief Switch the UI into full-screen preview mode.
 * @param ui UI context.
 * @return None.
 */
static void thermal_gui_enter_fullscreen(lv_ui *ui)
{
    if ((ui == NULL) || (g_preview_fullscreen_active != 0U))
    {
        return;
    }

    g_preview_fullscreen_active = 1U;
    lv_scr_load(ui->WidgetsDemo_fullscreen);
}

/**
 * @brief Return from full-screen preview mode back to the settings page.
 * @param ui UI context.
 * @return None.
 */
static void thermal_gui_exit_fullscreen(lv_ui *ui)
{
    if ((ui == NULL) || (g_preview_fullscreen_active == 0U))
    {
        return;
    }

    g_preview_fullscreen_active = 0U;
    lv_scr_load(ui->WidgetsDemo);
}

/**
 * @brief Refresh the contrast slider value text.
 * @param contrast_slider_value Slider value in range 0..100.
 * @return None.
 */
static void thermal_gui_refresh_contrast_value(uint8_t contrast_slider_value)
{
    char value_text[8];

    (void)snprintf(value_text, sizeof(value_text), "%u", (unsigned)contrast_slider_value);
    if (g_contrast_value_label != NULL)
    {
        lv_label_set_text(g_contrast_value_label, value_text);
    }
}

/**
 * @brief Refresh the pseudo-color selection state.
 * @param ui UI context.
 * @param active_index Active pseudo-color index.
 * @return None.
 */
static void thermal_gui_refresh_pseudo_ui(lv_ui *ui, uint8_t active_index)
{
    char current_text[32];
    uint32_t child_count;
    uint32_t child_index;

    if (ui == NULL)
    {
        return;
    }

    if (active_index >= CFG_THERMAL_GUI_PSEUDO_COUNT)
    {
        active_index = 0U;
    }

    child_count = lv_obj_get_child_cnt(ui->WidgetsDemo_tab_palette);
    for (child_index = 0U; child_index < child_count; child_index++)
    {
        lv_obj_t *child = lv_obj_get_child(ui->WidgetsDemo_tab_palette, child_index);

        if ((child != NULL) && (lv_obj_get_class(child) == &lv_btn_class) && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE))
        {
            lv_coord_t x = lv_obj_get_x(child);
            lv_coord_t y = lv_obj_get_y(child);
            uint32_t i;

            for (i = 0U; i < CFG_THERMAL_GUI_PSEUDO_COUNT; i++)
            {
                if ((g_pseudo_map[i].x == x) && (g_pseudo_map[i].y == y))
                {
                    thermal_gui_set_button_selected(child, (i == active_index), lv_color_hex(0xffb454));
                    break;
                }
            }
        }
    }

    (void)snprintf(current_text, sizeof(current_text), "当前: %s", g_pseudo_map[active_index].name);
    thermal_gui_set_badge_text(ui->WidgetsDemo_btn_palette_current, current_text);
    thermal_gui_set_badge_text(ui->WidgetsDemo_status_pseudo, g_pseudo_map[active_index].name);
    thermal_gui_set_badge_text(ui->WidgetsDemo_preview_palette_name, g_pseudo_map[active_index].name);
    thermal_gui_set_badge_text(ui->WidgetsDemo_fullscreen_preview_palette_name, g_pseudo_map[active_index].name);
}

/**
 * @brief Update gain badge text.
 * @param ui UI context.
 * @return None.
 */
static void thermal_gui_refresh_gain_ui(lv_ui *ui)
{
    if (ui == NULL)
    {
        return;
    }

    thermal_gui_set_badge_text(ui->WidgetsDemo_status_gain, g_current_gain_high ? "增益 高" : "增益 低");
    thermal_gui_set_button_selected(ui->WidgetsDemo_btn_gain_high, g_current_gain_high != 0U, lv_color_hex(0xffb454));
    thermal_gui_set_button_selected(ui->WidgetsDemo_btn_gain_low, g_current_gain_high == 0U, lv_color_hex(0x7bc6ff));
}

/**
 * @brief Update the temperature unit buttons.
 * @param ui UI context.
 * @return None.
 */
static void thermal_gui_refresh_unit_ui(lv_ui *ui)
{
    if (ui == NULL)
    {
        return;
    }

    thermal_gui_set_button_selected(ui->WidgetsDemo_btn_unit_c, g_current_unit_celsius != 0U, lv_color_hex(0xffb454));
    thermal_gui_set_button_selected(ui->WidgetsDemo_btn_unit_f, g_current_unit_celsius == 0U, lv_color_hex(0x6b7885));
}

/**
 * @brief Update FFC badge text.
 * @param ui UI context.
 * @return None.
 */
static void thermal_gui_refresh_ffc_ui(lv_ui *ui)
{
    if (ui == NULL)
    {
        return;
    }

    thermal_gui_set_badge_text(ui->WidgetsDemo_status_ffc, g_current_auto_ffc ? "FFC 自动" : "FFC 手动");
    if (g_current_auto_ffc != 0U)
    {
        lv_obj_add_state(ui->WidgetsDemo_sw_auto_ffc, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_clear_state(ui->WidgetsDemo_sw_auto_ffc, LV_STATE_CHECKED);
    }
}

/**
 * @brief Refresh the device information card with the current UI summary.
 * @param ui UI context.
 * @return None.
 */
static void thermal_gui_refresh_device_info(lv_ui *ui)
{
    lv_obj_t *device_info_label;
    char summary[160];
    const char *unit_text = g_current_unit_celsius ? "℃" : "℉";

    if (ui == NULL)
    {
        return;
    }

    device_info_label = lv_obj_get_child(ui->WidgetsDemo_btn_device_info, 0);
    if (device_info_label == NULL)
    {
        return;
    }

    (void)snprintf(summary,
                   sizeof(summary),
                   "设备信息\nSTM32N647 + Tiny1C\n%s 对比%u%% AI%u\n镜像%u 翻转%u 十字%u 温显%u 标记%u\n保存%u",
                   unit_text,
                   (unsigned)(50U + g_current_contrast_slider),
                   (unsigned)g_current_ai_detect_enable,
                   (unsigned)g_current_mirror_enable,
                   (unsigned)g_current_flip_enable,
                   (unsigned)g_current_center_cross_enable,
                   (unsigned)g_current_center_temp_enable,
                   (unsigned)g_current_hi_lo_mark_enable,
                   (unsigned)g_config_saved);
    lv_label_set_text(device_info_label, summary);
}

/**
 * @brief Handle pseudo-color button clicks.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_pseudo_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    uintptr_t mode_index_value = (uintptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_CLICKED)
    {
        if (mode_index_value < CFG_THERMAL_GUI_PSEUDO_COUNT)
        {
            if (tiny1c_vdcmd_set_pseudo_color(PREVIEW_PATH0, g_pseudo_map[mode_index_value].mode) == IR_SUCCESS)
            {
                g_current_pseudo_index = (uint8_t)mode_index_value;
                app_thermal_ai_set_preview_pseudo_mode((uint8_t)g_pseudo_map[mode_index_value].mode);
                thermal_gui_refresh_pseudo_ui(&guider_ui, g_current_pseudo_index);
            }
        }
    }
}

/**
 * @brief Handle gain button clicks.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_gain_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    uintptr_t gain_high_value = (uintptr_t)lv_event_get_user_data(e);

    if (code == LV_EVENT_CLICKED)
    {
        if (tiny1c_vdcmd_set_gain_mode((uint8_t)gain_high_value) == IR_SUCCESS)
        {
            g_current_gain_high = (uint8_t)gain_high_value;
            tiny1c_thermal_app_set_gain_high(g_current_gain_high);
            thermal_gui_refresh_gain_ui(&guider_ui);
        }
    }
}

/**
 * @brief Handle automatic FFC switch changes.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_auto_ffc_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *sw = lv_event_get_target(e);

    if (code == LV_EVENT_VALUE_CHANGED)
    {
        uint8_t enable = (lv_obj_has_state(sw, LV_STATE_CHECKED) != 0U) ? 1U : 0U;

        if (tiny1c_vdcmd_set_auto_shutter_enabled(enable) == IR_SUCCESS)
        {
            g_current_auto_ffc = enable;
            thermal_gui_refresh_ffc_ui(&guider_ui);
        }
    }
}

/**
 * @brief Handle manual FFC requests.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_manual_ffc_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        (void)tiny1c_thermal_app_force_ffc();
        thermal_gui_set_badge_text(guider_ui.WidgetsDemo_status_ffc, "FFC 手动已触发");
    }
}

/**
 * @brief Handle save-config button clicks.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_save_config_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        g_config_saved = 1U;
        thermal_gui_set_badge_text(guider_ui.WidgetsDemo_status_uart, "串口 在线 | 已记录");
        thermal_gui_refresh_device_info(&guider_ui);
    }
}

/**
 * @brief Handle the full-screen button.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_fullscreen_enter_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        thermal_gui_enter_fullscreen((lv_ui *)lv_event_get_user_data(e));
    }
}

/**
 * @brief Handle the settings button on the full-screen page.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_fullscreen_exit_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        thermal_gui_exit_fullscreen((lv_ui *)lv_event_get_user_data(e));
    }
}

/**
 * @brief Handle the Celsius unit button.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_unit_c_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        g_current_unit_celsius = 1U;
        thermal_gui_refresh_unit_ui(&guider_ui);
        thermal_gui_refresh_device_info(&guider_ui);
    }
}

/**
 * @brief Handle the Fahrenheit unit button.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_unit_f_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        g_current_unit_celsius = 0U;
        thermal_gui_refresh_unit_ui(&guider_ui);
        thermal_gui_refresh_device_info(&guider_ui);
    }
}

/**
 * @brief Handle the contrast slider.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_contrast_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *slider = lv_event_get_target(e);

    if (code == LV_EVENT_VALUE_CHANGED)
    {
        g_current_contrast_slider = (uint8_t)lv_slider_get_value(slider);
        tiny1c_thermal_app_set_preview_contrast(g_current_contrast_slider);
        thermal_gui_refresh_contrast_value(g_current_contrast_slider);
        thermal_gui_refresh_device_info(&guider_ui);
    }
}

/**
 * @brief Handle the AI detection switch.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_ai_detect_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        g_current_ai_detect_enable = (lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED) != 0U) ? 1U : 0U;
        app_thermal_ai_set_enabled(g_current_ai_detect_enable);
        thermal_gui_apply_preview_overlay_state(&guider_ui);
        thermal_gui_refresh_device_info(&guider_ui);
    }
}

/**
 * @brief Handle the mirror switch.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_mirror_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        g_current_mirror_enable = (lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED) != 0U) ? 1U : 0U;
        thermal_gui_apply_preview_transform();
        thermal_gui_refresh_device_info(&guider_ui);
    }
}

/**
 * @brief Handle the flip switch.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_flip_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        g_current_flip_enable = (lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED) != 0U) ? 1U : 0U;
        thermal_gui_apply_preview_transform();
        thermal_gui_refresh_device_info(&guider_ui);
    }
}

/**
 * @brief Handle the center-cross switch.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_center_cross_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        g_current_center_cross_enable = (lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED) != 0U) ? 1U : 0U;
        thermal_gui_apply_preview_overlay_state(&guider_ui);
        thermal_gui_refresh_device_info(&guider_ui);
    }
}

/**
 * @brief Handle the center-temperature switch.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_center_temp_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        g_current_center_temp_enable = (lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED) != 0U) ? 1U : 0U;
        thermal_gui_apply_preview_overlay_state(&guider_ui);
        thermal_gui_refresh_device_info(&guider_ui);
    }
}

/**
 * @brief Handle the hi/lo mark switch.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_hi_lo_mark_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        g_current_hi_lo_mark_enable = (lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED) != 0U) ? 1U : 0U;
        thermal_gui_apply_preview_overlay_state(&guider_ui);
        thermal_gui_refresh_device_info(&guider_ui);
    }
}

/**
 * @brief Bind pseudo-color buttons by their on-screen positions.
 * @param ui UI context.
 * @return None.
 */
static void thermal_gui_bind_pseudo_buttons(lv_ui *ui)
{
    uint32_t child_count;
    uint32_t child_index;

    if (ui == NULL)
    {
        return;
    }

    child_count = lv_obj_get_child_cnt(ui->WidgetsDemo_tab_palette);
    for (child_index = 0U; child_index < child_count; child_index++)
    {
        lv_obj_t *child = lv_obj_get_child(ui->WidgetsDemo_tab_palette, child_index);
        lv_coord_t x;
        lv_coord_t y;
        uint32_t map_index;

        if ((child == NULL) || (lv_obj_get_class(child) != &lv_btn_class) || !lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE))
        {
            continue;
        }

        x = lv_obj_get_x(child);
        y = lv_obj_get_y(child);
        for (map_index = 0U; map_index < CFG_THERMAL_GUI_PSEUDO_COUNT; map_index++)
        {
            if ((g_pseudo_map[map_index].x == x) && (g_pseudo_map[map_index].y == y))
            {
                lv_obj_add_event_cb(child, thermal_gui_pseudo_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)map_index);
                break;
            }
        }
    }
}

/**
 * @brief Update the current device information card.
 * @param ui UI context.
 * @return None.
 */
static void thermal_gui_update_device_info(lv_ui *ui)
{
    if (ui == NULL)
    {
        return;
    }

    lv_label_set_text(ui->WidgetsDemo_label_title, "热成像设置");
    thermal_gui_refresh_device_info(ui);
}

/**
 * @brief Screen load event handler.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void WidgetsDemo_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_ui *ui = lv_event_get_user_data(e);

    if (code == LV_EVENT_SCREEN_LOADED)
    {
        uint8_t pseudo_mode = 0U;
        uint8_t index;

        if ((ui != NULL) && (tiny1c_vdcmd_get_pseudo_color(PREVIEW_PATH0, &pseudo_mode) == IR_SUCCESS))
        {
            if (pseudo_mode == (uint8_t)PSEUDO_COLOR_MODE_2)
            {
                pseudo_mode = (uint8_t)PSEUDO_COLOR_MODE_5;
            }
            for (index = 0U; index < CFG_THERMAL_GUI_PSEUDO_COUNT; index++)
            {
                if ((uint8_t)g_pseudo_map[index].mode == pseudo_mode)
                {
                    g_current_pseudo_index = index;
                    break;
                }
            }
            app_thermal_ai_set_preview_pseudo_mode(pseudo_mode);
        }

        lv_obj_clear_flag(ui->WidgetsDemo, LV_OBJ_FLAG_SCROLLABLE);
        thermal_gui_bind_pseudo_buttons(ui);
        thermal_gui_update_device_info(ui);
        thermal_gui_refresh_pseudo_ui(ui, g_current_pseudo_index);
        thermal_gui_refresh_gain_ui(ui);
        thermal_gui_refresh_unit_ui(ui);
        thermal_gui_refresh_ffc_ui(ui);
        g_current_ai_detect_enable = app_thermal_ai_is_enabled();
        thermal_gui_apply_preview_overlay_state(ui);
        lv_slider_set_value(ui->WidgetsDemo_slider_contrast, g_current_contrast_slider, LV_ANIM_OFF);
        tiny1c_thermal_app_set_preview_contrast(g_current_contrast_slider);
        thermal_gui_refresh_contrast_value(g_current_contrast_slider);
        thermal_gui_apply_preview_transform();
        thermal_gui_set_badge_text(ui->WidgetsDemo_status_uart, g_config_saved ? "串口 在线 | 已记录" : "串口 在线");
    }
}

/**
 * @brief Bind events for the thermal prototype screen.
 * @param ui UI context.
 * @return None.
 */
void events_init_WidgetsDemo(lv_ui *ui)
{
    lv_obj_add_event_cb(ui->WidgetsDemo, WidgetsDemo_event_handler, LV_EVENT_ALL, ui);

    lv_obj_add_event_cb(ui->WidgetsDemo_btn_gain_high, thermal_gui_gain_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)1U);
    lv_obj_add_event_cb(ui->WidgetsDemo_btn_gain_low, thermal_gui_gain_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)0U);
    lv_obj_add_event_cb(ui->WidgetsDemo_sw_auto_ffc, thermal_gui_auto_ffc_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui->WidgetsDemo_btn_ffc_manual, thermal_gui_manual_ffc_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui->WidgetsDemo_btn_save_cfg, thermal_gui_save_config_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui->WidgetsDemo_btn_unit_c, thermal_gui_unit_c_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui->WidgetsDemo_btn_unit_f, thermal_gui_unit_f_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ui->WidgetsDemo_slider_contrast, thermal_gui_contrast_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui->WidgetsDemo_sw_ai_detect, thermal_gui_ai_detect_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui->WidgetsDemo_sw_mirror, thermal_gui_mirror_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui->WidgetsDemo_sw_flip, thermal_gui_flip_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui->WidgetsDemo_sw_center_cross, thermal_gui_center_cross_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui->WidgetsDemo_sw_center_temp, thermal_gui_center_temp_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui->WidgetsDemo_sw_hi_lo_mark, thermal_gui_hi_lo_mark_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(ui->WidgetsDemo_btn_fullscreen, thermal_gui_fullscreen_enter_event_cb, LV_EVENT_CLICKED, ui);
}

/**
 * @brief Initialize all generated event bindings.
 * @param ui UI context.
 * @return None.
 */
void events_init(lv_ui *ui)
{
    (void)ui;
}

/**
 * @brief Bind events for the full-screen preview screen.
 * @param ui UI context.
 * @return None.
 */
void events_init_WidgetsDemoFullscreen(lv_ui *ui)
{
    lv_obj_add_event_cb(ui->WidgetsDemo_fullscreen_btn_settings, thermal_gui_fullscreen_exit_event_cb, LV_EVENT_CLICKED, ui);
}

uint8_t thermal_gui_is_fullscreen_active(void)
{
    return g_preview_fullscreen_active;
}

uint8_t thermal_gui_is_unit_celsius(void)
{
    return g_current_unit_celsius;
}
