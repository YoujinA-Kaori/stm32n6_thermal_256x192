/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#ifndef GUI_GUIDER_H
#define GUI_GUIDER_H
#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "lv_conf_ext.h"

typedef struct
{
    lv_obj_t *WidgetsDemo;
    bool WidgetsDemo_del;
    bool WidgetsDemo_fullscreen_del;

    lv_obj_t *WidgetsDemo_status_bar;
    lv_obj_t *WidgetsDemo_label_title;
    lv_obj_t *WidgetsDemo_status_pseudo;
    lv_obj_t *WidgetsDemo_status_gain;
    lv_obj_t *WidgetsDemo_status_ffc;
    lv_obj_t *WidgetsDemo_status_uart;
    lv_obj_t *WidgetsDemo_status_time;
    lv_obj_t *WidgetsDemo_status_power;

    lv_obj_t *WidgetsDemo_left_panel;
    lv_obj_t *WidgetsDemo_tabview_ctrl;
    lv_obj_t *WidgetsDemo_tab_palette;
    lv_obj_t *WidgetsDemo_tab_display;
    lv_obj_t *WidgetsDemo_tab_module;
    lv_obj_t *WidgetsDemo_tab_system;
    lv_obj_t *WidgetsDemo_btn_palette_current;
    lv_obj_t *WidgetsDemo_pseudo_buttons[11];
    lv_obj_t *WidgetsDemo_slider_brightness;
    lv_obj_t *WidgetsDemo_slider_contrast;
    lv_obj_t *WidgetsDemo_sw_mirror;
    lv_obj_t *WidgetsDemo_sw_flip;
    lv_obj_t *WidgetsDemo_sw_center_cross;
    lv_obj_t *WidgetsDemo_sw_center_temp;
    lv_obj_t *WidgetsDemo_sw_hi_lo_mark;
    lv_obj_t *WidgetsDemo_btn_unit_c;
    lv_obj_t *WidgetsDemo_btn_unit_f;
    lv_obj_t *WidgetsDemo_btn_gain_high;
    lv_obj_t *WidgetsDemo_btn_gain_low;
    lv_obj_t *WidgetsDemo_sw_auto_ffc;
    lv_obj_t *WidgetsDemo_btn_ffc_manual;
    lv_obj_t *WidgetsDemo_slider_screen;
    lv_obj_t *WidgetsDemo_sw_ai_detect;
    lv_obj_t *WidgetsDemo_btn_save_cfg;
    lv_obj_t *WidgetsDemo_btn_device_info;

    lv_obj_t *WidgetsDemo_preview_panel;
    lv_obj_t *WidgetsDemo_preview_frame;
    lv_obj_t *WidgetsDemo_preview_img;
    lv_obj_t *WidgetsDemo_preview_cross_h;
    lv_obj_t *WidgetsDemo_preview_cross_v;
    lv_obj_t *WidgetsDemo_preview_label;
    lv_obj_t *WidgetsDemo_preview_center_temp;
    lv_obj_t *WidgetsDemo_preview_max_temp;
    lv_obj_t *WidgetsDemo_preview_min_temp;
    lv_obj_t *WidgetsDemo_preview_palette_name;
    lv_obj_t *WidgetsDemo_preview_max_marker;
    lv_obj_t *WidgetsDemo_preview_min_marker;
    lv_obj_t *WidgetsDemo_btn_fullscreen;

    lv_obj_t *WidgetsDemo_footer_bar;
    lv_obj_t *WidgetsDemo_btn_save;
    lv_obj_t *WidgetsDemo_btn_ffc;
    lv_obj_t *WidgetsDemo_btn_snapshot;
    lv_obj_t *WidgetsDemo_btn_alarm;

    lv_obj_t *WidgetsDemo_fullscreen;
    lv_obj_t *WidgetsDemo_fullscreen_preview_frame;
    lv_obj_t *WidgetsDemo_fullscreen_preview_img;
    lv_obj_t *WidgetsDemo_fullscreen_preview_cross_h;
    lv_obj_t *WidgetsDemo_fullscreen_preview_cross_v;
    lv_obj_t *WidgetsDemo_fullscreen_preview_center_temp;
    lv_obj_t *WidgetsDemo_fullscreen_preview_max_temp;
    lv_obj_t *WidgetsDemo_fullscreen_preview_min_temp;
    lv_obj_t *WidgetsDemo_fullscreen_preview_palette_name;
    lv_obj_t *WidgetsDemo_fullscreen_preview_max_marker;
    lv_obj_t *WidgetsDemo_fullscreen_preview_min_marker;
    lv_obj_t *WidgetsDemo_fullscreen_btn_settings;

    lv_obj_t *g_kb_top_layer;
} lv_ui;

typedef void (*ui_setup_scr_t)(lv_ui * ui);

void ui_init_style(lv_style_t * style);

void ui_load_scr_animation(lv_ui *ui, lv_obj_t ** new_scr, bool new_scr_del, bool * old_scr_del, ui_setup_scr_t setup_scr,
                           lv_scr_load_anim_t anim_type, uint32_t time, uint32_t delay, bool is_clean, bool auto_del);

void ui_animation(void * var, int32_t duration, int32_t delay, int32_t start_value, int32_t end_value, lv_anim_path_cb_t path_cb,
                  uint16_t repeat_cnt, uint32_t repeat_delay, uint32_t playback_time, uint32_t playback_delay,
                  lv_anim_exec_xcb_t exec_cb, lv_anim_start_cb_t start_cb, lv_anim_ready_cb_t ready_cb, lv_anim_deleted_cb_t deleted_cb);

void init_scr_del_flag(lv_ui *ui);
void setup_ui(lv_ui *ui);
void init_keyboard(lv_ui *ui);

extern lv_ui guider_ui;

void setup_scr_WidgetsDemo(lv_ui *ui);
void setup_scr_WidgetsDemoFullscreen(lv_ui *ui);

LV_FONT_DECLARE(lv_font_montserratMedium_19)
LV_FONT_DECLARE(lv_font_montserratMedium_23)
LV_FONT_DECLARE(lv_font_montserratMedium_26)
LV_FONT_DECLARE(lv_font_montserratMedium_32)
LV_FONT_DECLARE(lv_font_thermal_cn_18)

uint8_t thermal_gui_is_unit_celsius(void);

#ifdef __cplusplus
}
#endif
#endif
