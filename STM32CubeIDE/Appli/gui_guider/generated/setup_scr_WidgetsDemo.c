/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#include "lvgl.h"
#include <stdio.h>
#include <string.h>
#include "gui_guider.h"
#include "events_init.h"
#include "widgets_init.h"
#include "custom.h"
#include "images/thermal_ai_banner_234x50.h"

#ifndef THERMAL_GUI_TEXT_FONT
#define THERMAL_GUI_TEXT_FONT (&lv_font_montserratMedium_19)
#endif

#ifndef THERMAL_GUI_CN_FONT
#define THERMAL_GUI_CN_FONT (&lv_font_thermal_cn_18)
#endif

#define THERMAL_GUI_SCREEN_WIDTH             (800)
#define THERMAL_GUI_SCREEN_HEIGHT            (480)
#define THERMAL_GUI_STATUS_HEIGHT            (52)
#define THERMAL_GUI_BOTTOM_HEIGHT            (56)
#define THERMAL_GUI_CONTENT_TOP              (60)
#define THERMAL_GUI_CONTENT_HEIGHT           (352)
#define THERMAL_GUI_LEFT_WIDTH               (300)
#define THERMAL_GUI_RIGHT_WIDTH              (464)
#define THERMAL_GUI_MARGIN                   (12)
#define THERMAL_GUI_PREVIEW_WIDTH            (432)
#define THERMAL_GUI_PREVIEW_HEIGHT           (324)
#define THERMAL_GUI_PREVIEW_IMG_X            (0)
#define THERMAL_GUI_PREVIEW_IMG_Y            (0)
#define THERMAL_GUI_PREVIEW_IMG_ZOOM         (256U)
#define THERMAL_GUI_PREVIEW_CROSS_LEN        (28)
#define THERMAL_GUI_FULLSCREEN_PREVIEW_WIDTH (640)
#define THERMAL_GUI_FULLSCREEN_PREVIEW_HEIGHT (480)
#define THERMAL_GUI_FULLSCREEN_PREVIEW_X     (80)
#define THERMAL_GUI_FULLSCREEN_PREVIEW_Y     (0)
#define THERMAL_GUI_FULLSCREEN_PREVIEW_CROSS_LEN (40)
#define THERMAL_GUI_FULLSCREEN_BUTTON_X      (724)
#define THERMAL_GUI_FULLSCREEN_BUTTON_Y      (10)
#define THERMAL_GUI_FULLSCREEN_BUTTON_WIDTH  (66)
#define THERMAL_GUI_FULLSCREEN_BUTTON_HEIGHT (40)

/* Restrained field-instrument palette: black surfaces, one warm action accent,
 * and semantic colors only for temperature and link states. */
#define THERMAL_GUI_COLOR_BG                 (0x050709)
#define THERMAL_GUI_COLOR_SURFACE            (0x0b0f13)
#define THERMAL_GUI_COLOR_SURFACE_RAISED     (0x11171d)
#define THERMAL_GUI_COLOR_BORDER             (0x26313b)
#define THERMAL_GUI_COLOR_BORDER_ACTIVE      (0x475866)
#define THERMAL_GUI_COLOR_TEXT               (0xf1f5f7)
#define THERMAL_GUI_COLOR_TEXT_MUTED         (0x9aa7b2)
#define THERMAL_GUI_COLOR_ACCENT             (0xffa23a)
#define THERMAL_GUI_COLOR_HOT                (0xff7248)
#define THERMAL_GUI_COLOR_COLD               (0x45bfff)
#define THERMAL_GUI_COLOR_OK                 (0x35cf91)

lv_obj_t *g_contrast_value_label = NULL;

/**
 * @brief Configure the base visual style of a panel container.
 * @param panel Target panel object.
 * @param bg_color Background color.
 * @param border_color Border color.
 * @param radius Border radius.
 * @return None.
 */
static void thermal_gui_style_panel(lv_obj_t *panel, lv_color_t bg_color, lv_color_t border_color, lv_coord_t radius)
{
    lv_obj_set_style_radius(panel, radius, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(panel, border_color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(panel, LV_OPA_60, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(panel, bg_color, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
}

/**
 * @brief Create a status badge used by the top status bar.
 * @param parent Parent container.
 * @param text Badge text.
 * @param accent Accent color.
 * @param pos_x X coordinate relative to parent.
 * @param width Badge width.
 * @return Created badge container.
 */
static lv_obj_t *thermal_gui_create_status_badge(lv_obj_t *parent, const char *text, lv_color_t accent,
                                                 lv_coord_t pos_x, lv_coord_t width)
{
    lv_obj_t *badge = lv_obj_create(parent);
    lv_obj_t *accent_line = NULL;

    lv_obj_set_pos(badge, pos_x, 8);
    lv_obj_set_size(badge, width, 34);
    lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    thermal_gui_style_panel(badge, lv_color_hex(THERMAL_GUI_COLOR_SURFACE_RAISED),
                            lv_color_hex(THERMAL_GUI_COLOR_BORDER), 6);
    lv_obj_set_style_border_opa(badge, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *label = lv_label_create(badge);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, width - 14);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(label, LV_ALIGN_CENTER, 3, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(THERMAL_GUI_COLOR_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, THERMAL_GUI_CN_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);

    accent_line = lv_obj_create(badge);
    lv_obj_set_pos(accent_line, 0, 6);
    lv_obj_set_size(accent_line, 3, 22);
    lv_obj_clear_flag(accent_line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(accent_line, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(accent_line, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(accent_line, accent, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(accent_line, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    return badge;
}

/**
 * @brief Create a compact action button with optional highlighted state.
 * @param parent Parent container.
 * @param text Button label.
 * @param pos_x X coordinate relative to parent.
 * @param width Button width.
 * @param accent Accent color.
 * @param is_active True to render as active selection.
 * @return Created button object.
 */
static lv_obj_t *thermal_gui_create_action_button(lv_obj_t *parent, const char *text, lv_coord_t pos_x,
                                                  lv_coord_t width, lv_color_t accent, bool is_active)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_pos(button, pos_x, 0);
    lv_obj_set_size(button, width, 38);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(button, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(button, is_active ? accent : lv_color_hex(THERMAL_GUI_COLOR_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, is_active ? lv_color_hex(0x24201b) : lv_color_hex(THERMAL_GUI_COLOR_SURFACE_RAISED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x202a32), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, is_active ? accent : lv_color_hex(THERMAL_GUI_COLOR_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, THERMAL_GUI_CN_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);

    return button;
}

/**
 * @brief Create a label and slider row for parameter adjustment.
 * @param parent Parent container.
 * @param title Parameter title.
 * @param value_text Right-side value text.
 * @param pos_y Y coordinate relative to parent.
 * @param default_value Initial slider value.
 * @return Created slider object.
 */
static lv_obj_t *thermal_gui_create_slider_row(lv_obj_t *parent, const char *title, const char *value_text,
                                               lv_coord_t pos_y, int32_t default_value)
{
    lv_obj_t *title_label = lv_label_create(parent);
    lv_label_set_text(title_label, title);
    lv_obj_set_pos(title_label, 8, pos_y);
    lv_obj_set_style_text_color(title_label, lv_color_hex(THERMAL_GUI_COLOR_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(title_label, THERMAL_GUI_CN_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *value_label = lv_label_create(parent);
    lv_label_set_text(value_label, value_text);
    lv_obj_align_to(value_label, title_label, LV_ALIGN_OUT_RIGHT_MID, 168, 0);
    lv_obj_set_width(value_label, 48);
    lv_obj_set_style_text_color(value_label, lv_color_hex(THERMAL_GUI_COLOR_ACCENT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(value_label, THERMAL_GUI_TEXT_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);

    if ((title != NULL) && (strcmp(title, "对比度") == 0))
    {
        g_contrast_value_label = value_label;
    }

    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_pos(slider, 8, pos_y + 28);
    lv_obj_set_size(slider, 250, 8);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, default_value, LV_ANIM_OFF);
    lv_obj_set_style_bg_opa(slider, LV_OPA_40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, lv_color_hex(THERMAL_GUI_COLOR_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(slider, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, lv_color_hex(THERMAL_GUI_COLOR_ACCENT), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(slider, 8, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0xffc16f), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(slider, 10, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(slider, 4, LV_PART_KNOB | LV_STATE_DEFAULT);

    return slider;
}

/**
 * @brief Create a labeled switch row.
 * @param parent Parent container.
 * @param text Row label.
 * @param pos_y Y coordinate relative to parent.
 * @param is_checked True to set initial checked state.
 * @return Created switch object.
 */
static lv_obj_t *thermal_gui_create_switch_row(lv_obj_t *parent, const char *text, lv_coord_t pos_y, bool is_checked)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, 8, pos_y + 2);
    lv_obj_set_style_text_color(label, lv_color_hex(THERMAL_GUI_COLOR_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, THERMAL_GUI_CN_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_pos(sw, 204, pos_y);
    lv_obj_set_size(sw, 54, 28);
    if (is_checked)
    {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_set_style_bg_color(sw, lv_color_hex(THERMAL_GUI_COLOR_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(sw, lv_color_hex(THERMAL_GUI_COLOR_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0xf6f8fa), LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(sw, 20, LV_PART_KNOB | LV_STATE_DEFAULT);

    return sw;
}

/**
 * @brief Create a pseudo-color selection button with a miniature color strip.
 * @param parent Parent container.
 * @param text Mode name.
 * @param pos_x X coordinate relative to parent.
 * @param pos_y Y coordinate relative to parent.
 * @param strip_start First strip color.
 * @param strip_end Second strip color.
 * @param is_active True to render as selected mode.
 * @return Created button object.
 */
static lv_obj_t *thermal_gui_create_palette_button(lv_obj_t *parent, const char *text, lv_coord_t pos_x, lv_coord_t pos_y,
                                                   lv_color_t strip_start, lv_color_t strip_end, bool is_active)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_pos(button, pos_x, pos_y);
    lv_obj_set_size(button, 124, 42);
    lv_obj_set_style_radius(button, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(button, is_active ? lv_color_hex(THERMAL_GUI_COLOR_ACCENT) : lv_color_hex(THERMAL_GUI_COLOR_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, is_active ? lv_color_hex(0x24201b) : lv_color_hex(THERMAL_GUI_COLOR_SURFACE_RAISED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x202a32), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *strip = lv_obj_create(button);
    lv_obj_set_pos(strip, 7, 7);
    lv_obj_set_size(strip, 18, 28);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(strip, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(strip, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(strip, strip_start, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(strip, strip_end, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(strip, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(strip, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 32, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(THERMAL_GUI_COLOR_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, THERMAL_GUI_CN_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);

    return button;
}

/**
 * @brief Create a footer shortcut button.
 * @param parent Parent container.
 * @param text Button label.
 * @param pos_x X coordinate relative to parent.
 * @param width Button width.
 * @param accent Accent color.
 * @param emphasize True to render as emphasized shortcut.
 * @return Created button object.
 */
static lv_obj_t *thermal_gui_create_footer_button(lv_obj_t *parent, const char *text, lv_coord_t pos_x,
                                                  lv_coord_t width, lv_color_t accent, bool emphasize)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_pos(button, pos_x, 6);
    lv_obj_set_size(button, width, 44);
    lv_obj_set_style_radius(button, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(button, emphasize ? accent : lv_color_hex(THERMAL_GUI_COLOR_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, emphasize ? lv_color_hex(0x1c1b19) : lv_color_hex(THERMAL_GUI_COLOR_SURFACE_RAISED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x202a32), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, emphasize ? accent : lv_color_hex(THERMAL_GUI_COLOR_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, THERMAL_GUI_CN_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);

    return button;
}

/**
 * @brief Create a small high/low marker label with a dark background for thermal overlays.
 * @param parent Parent container.
 * @param text Marker text.
 * @param pos_x X coordinate relative to parent.
 * @param pos_y Y coordinate relative to parent.
 * @return Created label object.
 */
static lv_obj_t *thermal_gui_create_marker_label(lv_obj_t *parent, const char *text, lv_coord_t pos_x, lv_coord_t pos_y)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_pos(label, pos_x, pos_y);
    lv_obj_set_size(label, 22, 22);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(label, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(label, lv_color_hex(0x050709), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(label, LV_OPA_90, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(label, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(label, lv_color_hex(THERMAL_GUI_COLOR_BORDER_ACTIVE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(label, LV_OPA_90, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, THERMAL_GUI_CN_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    return label;
}

/**
 * @brief Build the industrial thermal camera GUI prototype screen.
 * @param ui UI context.
 * @return None.
 */
void setup_scr_WidgetsDemo(lv_ui *ui)
{
    lv_obj_t *label = NULL;
    lv_obj_t *page = NULL;
    lv_obj_t *note_box = NULL;
    lv_obj_t *title_accent = NULL;
    lv_obj_t *brand_panel = NULL;
    lv_obj_t *brand_img = NULL;
    ui->WidgetsDemo = lv_obj_create(NULL);
    lv_obj_set_size(ui->WidgetsDemo, THERMAL_GUI_SCREEN_WIDTH, THERMAL_GUI_SCREEN_HEIGHT);
    lv_obj_clear_flag(ui->WidgetsDemo, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(ui->WidgetsDemo, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(ui->WidgetsDemo, lv_color_hex(THERMAL_GUI_COLOR_BG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->WidgetsDemo, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->WidgetsDemo, lv_color_hex(THERMAL_GUI_COLOR_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui->WidgetsDemo_status_bar = lv_obj_create(ui->WidgetsDemo);
    lv_obj_set_pos(ui->WidgetsDemo_status_bar, 0, 0);
    lv_obj_set_size(ui->WidgetsDemo_status_bar, THERMAL_GUI_SCREEN_WIDTH, THERMAL_GUI_STATUS_HEIGHT);
    lv_obj_clear_flag(ui->WidgetsDemo_status_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui->WidgetsDemo_status_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->WidgetsDemo_status_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->WidgetsDemo_status_bar, lv_color_hex(THERMAL_GUI_COLOR_SURFACE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->WidgetsDemo_status_bar, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui->WidgetsDemo_status_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    title_accent = lv_obj_create(ui->WidgetsDemo_status_bar);
    lv_obj_set_pos(title_accent, 8, 10);
    lv_obj_set_size(title_accent, 3, 32);
    lv_obj_clear_flag(title_accent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(title_accent, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(title_accent, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(title_accent, lv_color_hex(THERMAL_GUI_COLOR_ACCENT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(title_accent, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui->WidgetsDemo_label_title = lv_label_create(ui->WidgetsDemo_status_bar);
    lv_label_set_text(ui->WidgetsDemo_label_title, "热成像");
    lv_obj_set_pos(ui->WidgetsDemo_label_title, 20, 14);
    lv_obj_set_style_text_font(ui->WidgetsDemo_label_title, THERMAL_GUI_CN_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->WidgetsDemo_label_title, lv_color_hex(THERMAL_GUI_COLOR_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui->WidgetsDemo_status_pseudo = thermal_gui_create_status_badge(ui->WidgetsDemo_status_bar, "伪彩 锐化", lv_color_hex(THERMAL_GUI_COLOR_ACCENT), 134, 106);
    ui->WidgetsDemo_status_gain = thermal_gui_create_status_badge(ui->WidgetsDemo_status_bar, "增益 高", lv_color_hex(THERMAL_GUI_COLOR_HOT), 246, 88);
    ui->WidgetsDemo_status_ffc = thermal_gui_create_status_badge(ui->WidgetsDemo_status_bar, "FFC 自动", lv_color_hex(THERMAL_GUI_COLOR_COLD), 340, 102);
    ui->WidgetsDemo_status_uart = thermal_gui_create_status_badge(ui->WidgetsDemo_status_bar, "串口 在线", lv_color_hex(THERMAL_GUI_COLOR_OK), 448, 132);
    ui->WidgetsDemo_status_time = thermal_gui_create_status_badge(ui->WidgetsDemo_status_bar, "16:45", lv_color_hex(THERMAL_GUI_COLOR_COLD), 586, 76);
    ui->WidgetsDemo_status_power = thermal_gui_create_status_badge(ui->WidgetsDemo_status_bar, "BAT --%", lv_color_hex(THERMAL_GUI_COLOR_ACCENT), 668, 120);

    ui->WidgetsDemo_left_panel = lv_obj_create(ui->WidgetsDemo);
    lv_obj_set_pos(ui->WidgetsDemo_left_panel, THERMAL_GUI_MARGIN, THERMAL_GUI_CONTENT_TOP);
    lv_obj_set_size(ui->WidgetsDemo_left_panel, THERMAL_GUI_LEFT_WIDTH, THERMAL_GUI_CONTENT_HEIGHT);
    lv_obj_clear_flag(ui->WidgetsDemo_left_panel, LV_OBJ_FLAG_SCROLLABLE);
    thermal_gui_style_panel(ui->WidgetsDemo_left_panel, lv_color_hex(THERMAL_GUI_COLOR_SURFACE),
                            lv_color_hex(THERMAL_GUI_COLOR_BORDER), 8);
    ui->WidgetsDemo_tabview_ctrl = lv_tabview_create(ui->WidgetsDemo_left_panel, LV_DIR_TOP, 40);
    lv_obj_set_pos(ui->WidgetsDemo_tabview_ctrl, 0, 0);
    lv_obj_set_size(ui->WidgetsDemo_tabview_ctrl, THERMAL_GUI_LEFT_WIDTH, THERMAL_GUI_CONTENT_HEIGHT);
    lv_obj_clear_flag(ui->WidgetsDemo_tabview_ctrl, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ui->WidgetsDemo_tabview_ctrl, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->WidgetsDemo_tabview_ctrl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->WidgetsDemo_tabview_ctrl, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui->WidgetsDemo_tabview_ctrl, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(lv_tabview_get_tab_btns(ui->WidgetsDemo_tabview_ctrl), lv_color_hex(THERMAL_GUI_COLOR_SURFACE_RAISED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(lv_tabview_get_tab_btns(ui->WidgetsDemo_tabview_ctrl), 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(lv_tabview_get_tab_btns(ui->WidgetsDemo_tabview_ctrl), 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lv_tabview_get_tab_btns(ui->WidgetsDemo_tabview_ctrl), lv_color_hex(THERMAL_GUI_COLOR_TEXT_MUTED), LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(lv_tabview_get_tab_btns(ui->WidgetsDemo_tabview_ctrl), LV_OPA_TRANSP, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(lv_tabview_get_tab_btns(ui->WidgetsDemo_tabview_ctrl), lv_color_hex(0x211d18), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(lv_tabview_get_tab_btns(ui->WidgetsDemo_tabview_ctrl), LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(lv_tabview_get_tab_btns(ui->WidgetsDemo_tabview_ctrl), lv_color_hex(THERMAL_GUI_COLOR_ACCENT), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(lv_tabview_get_tab_btns(ui->WidgetsDemo_tabview_ctrl), 1, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(lv_tabview_get_tab_btns(ui->WidgetsDemo_tabview_ctrl), lv_color_hex(THERMAL_GUI_COLOR_ACCENT), LV_PART_ITEMS | LV_STATE_CHECKED);

    ui->WidgetsDemo_tab_palette = lv_tabview_add_tab(ui->WidgetsDemo_tabview_ctrl, "伪彩");
    ui->WidgetsDemo_tab_display = lv_tabview_add_tab(ui->WidgetsDemo_tabview_ctrl, "显示");
    ui->WidgetsDemo_tab_module = lv_tabview_add_tab(ui->WidgetsDemo_tabview_ctrl, "模组");
    ui->WidgetsDemo_tab_system = lv_tabview_add_tab(ui->WidgetsDemo_tabview_ctrl, "系统");
    lv_obj_set_style_text_font(lv_tabview_get_tab_btns(ui->WidgetsDemo_tabview_ctrl), THERMAL_GUI_CN_FONT, LV_PART_ITEMS | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lv_tabview_get_tab_btns(ui->WidgetsDemo_tabview_ctrl), THERMAL_GUI_CN_FONT, LV_PART_ITEMS | LV_STATE_CHECKED);

    page = ui->WidgetsDemo_tab_palette;
    lv_obj_set_style_bg_color(page, lv_color_hex(THERMAL_GUI_COLOR_SURFACE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(page, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(page, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(page, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(page, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);

    label = lv_label_create(page);
    lv_label_set_text(label, "当前模式");
    lv_obj_set_pos(label, 8, 4);
    lv_obj_set_style_text_color(label, lv_color_hex(THERMAL_GUI_COLOR_TEXT_MUTED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, THERMAL_GUI_CN_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui->WidgetsDemo_btn_palette_current = thermal_gui_create_action_button(page, "当前: 锐化", 118, 132, lv_color_hex(0xffb454), true);
    lv_obj_set_pos(ui->WidgetsDemo_btn_palette_current, 126, 0);
    lv_obj_set_size(ui->WidgetsDemo_btn_palette_current, 132, 34);
    lv_obj_clear_flag(ui->WidgetsDemo_btn_palette_current, LV_OBJ_FLAG_CLICKABLE);

    thermal_gui_create_palette_button(page, "白热", 8, 46, lv_color_hex(0xe8edf2), lv_color_hex(0x737d87), false);
    thermal_gui_create_palette_button(page, "黑热", 140, 46, lv_color_hex(0x101214), lv_color_hex(0x737d87), false);
    thermal_gui_create_palette_button(page, "热像", 8, 94, lv_color_hex(0x3b0a09), lv_color_hex(0xff7a1a), false);
    thermal_gui_create_palette_button(page, "绿热", 140, 94, lv_color_hex(0x0d3f18), lv_color_hex(0xb8ff4d), false);
    thermal_gui_create_palette_button(page, "锐化", 8, 142, lv_color_hex(0x0f6f94), lv_color_hex(0xf4d15c), true);
    thermal_gui_create_palette_button(page, "户外", 140, 142, lv_color_hex(0x0f3845), lv_color_hex(0xe09337), false);
    thermal_gui_create_palette_button(page, "夜视", 8, 190, lv_color_hex(0x032b39), lv_color_hex(0x2ad2cf), false);
    thermal_gui_create_palette_button(page, "医疗", 140, 190, lv_color_hex(0x2b5078), lv_color_hex(0xf8b8aa), false);
    thermal_gui_create_palette_button(page, "测试", 8, 238, lv_color_hex(0x2f2f72), lv_color_hex(0xb4f36d), false);
    thermal_gui_create_palette_button(page, "铁红", 140, 238, lv_color_hex(0x3b0a09), lv_color_hex(0xff7a1a), false);

    note_box = lv_obj_create(page);
    lv_obj_set_pos(note_box, 8, 286);
    lv_obj_set_size(note_box, 250, 22);
    thermal_gui_style_panel(note_box, lv_color_hex(THERMAL_GUI_COLOR_SURFACE_RAISED),
                            lv_color_hex(THERMAL_GUI_COLOR_BORDER), 6);
    label = lv_label_create(note_box);
    lv_label_set_text(label, "常用伪彩优先");
    lv_obj_set_width(label, 224);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(THERMAL_GUI_COLOR_TEXT_MUTED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, THERMAL_GUI_CN_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);

    page = ui->WidgetsDemo_tab_display;
    lv_obj_set_style_bg_color(page, lv_color_hex(THERMAL_GUI_COLOR_SURFACE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(page, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);

    ui->WidgetsDemo_slider_contrast = thermal_gui_create_slider_row(page, "对比度", "54", 4, 54);
    ui->WidgetsDemo_sw_mirror = thermal_gui_create_switch_row(page, "镜像", 66, false);
    ui->WidgetsDemo_sw_flip = thermal_gui_create_switch_row(page, "翻转", 102, false);
    ui->WidgetsDemo_sw_center_cross = thermal_gui_create_switch_row(page, "中心十字", 138, true);
    ui->WidgetsDemo_sw_center_temp = thermal_gui_create_switch_row(page, "中心温度显示", 174, true);
    ui->WidgetsDemo_sw_hi_lo_mark = thermal_gui_create_switch_row(page, "最高温/最低温标记", 210, true);

    label = lv_label_create(page);
    lv_label_set_text(label, "温度单位");
    lv_obj_set_pos(label, 8, 258);
    lv_obj_set_style_text_color(label, lv_color_hex(THERMAL_GUI_COLOR_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, THERMAL_GUI_CN_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui->WidgetsDemo_btn_unit_c = thermal_gui_create_action_button(page, "℃", 148, 52, lv_color_hex(0xffb454), true);
    ui->WidgetsDemo_btn_unit_f = thermal_gui_create_action_button(page, "℉", 206, 52, lv_color_hex(0x6b7885), false);
    lv_obj_set_pos(ui->WidgetsDemo_btn_unit_c, 148, 250);
    lv_obj_set_pos(ui->WidgetsDemo_btn_unit_f, 206, 250);

    page = ui->WidgetsDemo_tab_module;
    lv_obj_set_style_bg_color(page, lv_color_hex(THERMAL_GUI_COLOR_SURFACE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(page, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);

    label = lv_label_create(page);
    lv_label_set_text(label, "增益模式");
    lv_obj_set_pos(label, 8, 6);
    lv_obj_set_style_text_color(label, lv_color_hex(THERMAL_GUI_COLOR_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, THERMAL_GUI_CN_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui->WidgetsDemo_btn_gain_high = thermal_gui_create_action_button(page, "高增益", 0, 112, lv_color_hex(0xffb454), true);
    ui->WidgetsDemo_btn_gain_low = thermal_gui_create_action_button(page, "低增益", 118, 112, lv_color_hex(0x71808d), false);
    lv_obj_set_pos(ui->WidgetsDemo_btn_gain_high, 8, 34);
    lv_obj_set_pos(ui->WidgetsDemo_btn_gain_low, 140, 34);

    ui->WidgetsDemo_sw_auto_ffc = thermal_gui_create_switch_row(page, "自动快门 / FFC", 88, true);

    label = lv_label_create(page);
    lv_label_set_text(label, "手动触发");
    lv_obj_set_pos(label, 8, 136);
    lv_obj_set_style_text_color(label, lv_color_hex(THERMAL_GUI_COLOR_TEXT), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, THERMAL_GUI_CN_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui->WidgetsDemo_btn_ffc_manual = thermal_gui_create_action_button(page, "执行 FFC", 140, 118, lv_color_hex(0x48b3ff), true);
    lv_obj_set_pos(ui->WidgetsDemo_btn_ffc_manual, 140, 126);
    lv_obj_set_size(ui->WidgetsDemo_btn_ffc_manual, 118, 38);

    note_box = lv_obj_create(page);
    lv_obj_set_pos(note_box, 8, 182);
    lv_obj_set_size(note_box, 250, 126);
    thermal_gui_style_panel(note_box, lv_color_hex(THERMAL_GUI_COLOR_SURFACE_RAISED),
                            lv_color_hex(THERMAL_GUI_COLOR_BORDER), 6);

    label = lv_label_create(note_box);
    lv_label_set_text(label, "模块图像状态\n- 当前模式: Image + Temperature\n- FFC 策略: 自动优先，手动补触发\n- 建议保留高增益用于常规测温场景");
    lv_obj_set_pos(label, 10, 10);
    lv_obj_set_width(label, 228);
    lv_obj_set_style_text_color(label, lv_color_hex(THERMAL_GUI_COLOR_TEXT_MUTED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(label, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, THERMAL_GUI_CN_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);

    page = ui->WidgetsDemo_tab_system;
    lv_obj_set_style_bg_color(page, lv_color_hex(THERMAL_GUI_COLOR_SURFACE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(page, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);

    ui->WidgetsDemo_sw_ai_detect = thermal_gui_create_switch_row(page, "AI检测", 6, false);

    ui->WidgetsDemo_btn_save_cfg = thermal_gui_create_action_button(page, "保存配置", 0, 148, lv_color_hex(0xffb454), true);
    lv_obj_set_pos(ui->WidgetsDemo_btn_save_cfg, 8, 58);

    ui->WidgetsDemo_btn_device_info = lv_obj_create(page);
    lv_obj_set_pos(ui->WidgetsDemo_btn_device_info, 8, 108);
    lv_obj_set_size(ui->WidgetsDemo_btn_device_info, 250, 104);
    thermal_gui_style_panel(ui->WidgetsDemo_btn_device_info, lv_color_hex(THERMAL_GUI_COLOR_SURFACE_RAISED),
                            lv_color_hex(THERMAL_GUI_COLOR_BORDER), 6);

    label = lv_label_create(ui->WidgetsDemo_btn_device_info);
    lv_label_set_text(label, "设备信息\nSTM32N647 + Tiny1C\n800x480 LVGL 原型");
    lv_obj_set_pos(label, 10, 10);
    lv_obj_set_width(label, 228);
    lv_obj_set_style_text_color(label, lv_color_hex(THERMAL_GUI_COLOR_TEXT_MUTED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(label, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(label, THERMAL_GUI_CN_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui->WidgetsDemo_preview_panel = lv_obj_create(ui->WidgetsDemo);
    lv_obj_set_pos(ui->WidgetsDemo_preview_panel, 324, THERMAL_GUI_CONTENT_TOP);
    lv_obj_set_size(ui->WidgetsDemo_preview_panel, THERMAL_GUI_RIGHT_WIDTH, THERMAL_GUI_CONTENT_HEIGHT);
    lv_obj_clear_flag(ui->WidgetsDemo_preview_panel, LV_OBJ_FLAG_SCROLLABLE);
    thermal_gui_style_panel(ui->WidgetsDemo_preview_panel, lv_color_hex(THERMAL_GUI_COLOR_SURFACE),
                            lv_color_hex(THERMAL_GUI_COLOR_BORDER), 8);

    ui->WidgetsDemo_preview_label = lv_label_create(ui->WidgetsDemo_preview_panel);
    lv_label_set_text(ui->WidgetsDemo_preview_label, "LIVE / 实时热成像");
    lv_obj_set_pos(ui->WidgetsDemo_preview_label, 14, 5);
    lv_obj_set_style_text_font(ui->WidgetsDemo_preview_label, THERMAL_GUI_CN_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->WidgetsDemo_preview_label, lv_color_hex(THERMAL_GUI_COLOR_OK), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui->WidgetsDemo_preview_frame = lv_obj_create(ui->WidgetsDemo_preview_panel);
    lv_obj_set_pos(ui->WidgetsDemo_preview_frame, 16, 28);
    lv_obj_set_size(ui->WidgetsDemo_preview_frame, THERMAL_GUI_PREVIEW_WIDTH, THERMAL_GUI_PREVIEW_HEIGHT);
    lv_obj_clear_flag(ui->WidgetsDemo_preview_frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui->WidgetsDemo_preview_frame, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->WidgetsDemo_preview_frame, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui->WidgetsDemo_preview_frame, lv_color_hex(THERMAL_GUI_COLOR_BORDER_ACTIVE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->WidgetsDemo_preview_frame, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->WidgetsDemo_preview_frame, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->WidgetsDemo_preview_frame, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui->WidgetsDemo_preview_frame, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->WidgetsDemo_preview_frame, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui->WidgetsDemo_preview_img = lv_img_create(ui->WidgetsDemo_preview_frame);
    lv_obj_set_pos(ui->WidgetsDemo_preview_img, THERMAL_GUI_PREVIEW_IMG_X, THERMAL_GUI_PREVIEW_IMG_Y);
    lv_obj_set_size(ui->WidgetsDemo_preview_img, THERMAL_GUI_PREVIEW_WIDTH, THERMAL_GUI_PREVIEW_HEIGHT);
    lv_obj_clear_flag(ui->WidgetsDemo_preview_img, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ui->WidgetsDemo_preview_img, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->WidgetsDemo_preview_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->WidgetsDemo_preview_img, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_img_set_zoom(ui->WidgetsDemo_preview_img, THERMAL_GUI_PREVIEW_IMG_ZOOM);

    ui->WidgetsDemo_preview_cross_h = lv_obj_create(ui->WidgetsDemo_preview_frame);
    lv_obj_set_pos(ui->WidgetsDemo_preview_cross_h,
                   THERMAL_GUI_PREVIEW_IMG_X + ((THERMAL_GUI_PREVIEW_WIDTH - THERMAL_GUI_PREVIEW_CROSS_LEN) / 2),
                   THERMAL_GUI_PREVIEW_IMG_Y + (THERMAL_GUI_PREVIEW_HEIGHT / 2));
    lv_obj_set_size(ui->WidgetsDemo_preview_cross_h, THERMAL_GUI_PREVIEW_CROSS_LEN, 1);
    lv_obj_set_style_bg_color(ui->WidgetsDemo_preview_cross_h, lv_color_hex(0xfafafa), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->WidgetsDemo_preview_cross_h, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->WidgetsDemo_preview_cross_h, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->WidgetsDemo_preview_cross_h, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui->WidgetsDemo_preview_cross_v = lv_obj_create(ui->WidgetsDemo_preview_frame);
    lv_obj_set_pos(ui->WidgetsDemo_preview_cross_v,
                   THERMAL_GUI_PREVIEW_IMG_X + (THERMAL_GUI_PREVIEW_WIDTH / 2),
                   THERMAL_GUI_PREVIEW_IMG_Y + ((THERMAL_GUI_PREVIEW_HEIGHT - THERMAL_GUI_PREVIEW_CROSS_LEN) / 2));
    lv_obj_set_size(ui->WidgetsDemo_preview_cross_v, 1, THERMAL_GUI_PREVIEW_CROSS_LEN);
    lv_obj_set_style_bg_color(ui->WidgetsDemo_preview_cross_v, lv_color_hex(0xfafafa), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->WidgetsDemo_preview_cross_v, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->WidgetsDemo_preview_cross_v, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->WidgetsDemo_preview_cross_v, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui->WidgetsDemo_preview_center_temp = thermal_gui_create_status_badge(ui->WidgetsDemo_preview_frame, "Center 36.8 C", lv_color_hex(THERMAL_GUI_COLOR_TEXT), 10, 132);
    lv_obj_set_pos(ui->WidgetsDemo_preview_center_temp, 10, 10);
    ui->WidgetsDemo_preview_max_temp = thermal_gui_create_status_badge(ui->WidgetsDemo_preview_frame, "Max 128.4 C", lv_color_hex(THERMAL_GUI_COLOR_HOT), 290, 132);
    lv_obj_set_pos(ui->WidgetsDemo_preview_max_temp, 290, 10);
    ui->WidgetsDemo_preview_min_temp = thermal_gui_create_status_badge(ui->WidgetsDemo_preview_frame, "Min 24.1 C", lv_color_hex(THERMAL_GUI_COLOR_COLD), 10, 132);
    lv_obj_set_pos(ui->WidgetsDemo_preview_min_temp, 10, 282);
    ui->WidgetsDemo_preview_palette_name = thermal_gui_create_status_badge(ui->WidgetsDemo_preview_frame, "锐化", lv_color_hex(THERMAL_GUI_COLOR_ACCENT), 314, 108);
    lv_obj_set_pos(ui->WidgetsDemo_preview_palette_name, 314, 282);

    ui->WidgetsDemo_preview_max_marker = thermal_gui_create_marker_label(ui->WidgetsDemo_preview_frame, "高", 350, 80);

    ui->WidgetsDemo_preview_min_marker = thermal_gui_create_marker_label(ui->WidgetsDemo_preview_frame, "低", 58, 230);

    ui->WidgetsDemo_btn_fullscreen = thermal_gui_create_footer_button(ui->WidgetsDemo_preview_panel, "全屏", 372, 76, lv_color_hex(THERMAL_GUI_COLOR_COLD), true);
    lv_obj_set_pos(ui->WidgetsDemo_btn_fullscreen, 376, 4);
    lv_obj_set_size(ui->WidgetsDemo_btn_fullscreen, 76, 28);

    ui->WidgetsDemo_footer_bar = lv_obj_create(ui->WidgetsDemo);
    lv_obj_set_pos(ui->WidgetsDemo_footer_bar, THERMAL_GUI_MARGIN, 420);
    lv_obj_set_size(ui->WidgetsDemo_footer_bar, 776, THERMAL_GUI_BOTTOM_HEIGHT);
    lv_obj_clear_flag(ui->WidgetsDemo_footer_bar, LV_OBJ_FLAG_SCROLLABLE);
    thermal_gui_style_panel(ui->WidgetsDemo_footer_bar, lv_color_hex(THERMAL_GUI_COLOR_SURFACE),
                            lv_color_hex(THERMAL_GUI_COLOR_BORDER), 8);

    ui->WidgetsDemo_btn_save = thermal_gui_create_footer_button(ui->WidgetsDemo_footer_bar, "保存", 8, 142, lv_color_hex(THERMAL_GUI_COLOR_ACCENT), true);
    ui->WidgetsDemo_btn_ffc = thermal_gui_create_footer_button(ui->WidgetsDemo_footer_bar, "手动 FFC", 160, 142, lv_color_hex(THERMAL_GUI_COLOR_COLD), true);
    ui->WidgetsDemo_btn_snapshot = NULL;
    ui->WidgetsDemo_btn_alarm = NULL;

    brand_panel = lv_obj_create(ui->WidgetsDemo_footer_bar);
    lv_obj_set_pos(brand_panel, 518, 2);
    lv_obj_set_size(brand_panel, 246, 52);
    lv_obj_clear_flag(brand_panel, LV_OBJ_FLAG_SCROLLABLE);
    thermal_gui_style_panel(brand_panel, lv_color_hex(0x030506), lv_color_hex(THERMAL_GUI_COLOR_BORDER), 6);
    lv_obj_set_style_border_opa(brand_panel, LV_OPA_70, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(brand_panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    brand_img = lv_img_create(brand_panel);
    lv_img_set_src(brand_img, &thermal_ai_banner_234x50);
    lv_obj_align(brand_img, LV_ALIGN_CENTER, 0, 0);

    events_init_WidgetsDemo(ui);
}

/**
 * @brief Build the full-screen preview screen.
 * @param ui UI context.
 * @return None.
 */
void setup_scr_WidgetsDemoFullscreen(lv_ui *ui)
{
    lv_obj_t *button = NULL;
    lv_obj_t *left_rail = NULL;
    lv_obj_t *right_rail = NULL;
    lv_obj_t *label = NULL;

    ui->WidgetsDemo_fullscreen = lv_obj_create(NULL);
    lv_obj_set_size(ui->WidgetsDemo_fullscreen, THERMAL_GUI_SCREEN_WIDTH, THERMAL_GUI_SCREEN_HEIGHT);
    lv_obj_clear_flag(ui->WidgetsDemo_fullscreen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(ui->WidgetsDemo_fullscreen, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(ui->WidgetsDemo_fullscreen, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->WidgetsDemo_fullscreen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->WidgetsDemo_fullscreen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui->WidgetsDemo_fullscreen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    left_rail = lv_obj_create(ui->WidgetsDemo_fullscreen);
    lv_obj_set_pos(left_rail, 0, 0);
    lv_obj_set_size(left_rail, THERMAL_GUI_FULLSCREEN_PREVIEW_X, THERMAL_GUI_SCREEN_HEIGHT);
    lv_obj_clear_flag(left_rail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(left_rail, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(left_rail, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(left_rail, LV_BORDER_SIDE_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(left_rail, lv_color_hex(THERMAL_GUI_COLOR_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(left_rail, lv_color_hex(THERMAL_GUI_COLOR_SURFACE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(left_rail, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(left_rail, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    label = lv_label_create(left_rail);
    lv_label_set_text(label, "LIVE");
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_text_font(label, THERMAL_GUI_TEXT_FONT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, lv_color_hex(THERMAL_GUI_COLOR_OK), LV_PART_MAIN | LV_STATE_DEFAULT);

    right_rail = lv_obj_create(ui->WidgetsDemo_fullscreen);
    lv_obj_set_pos(right_rail, THERMAL_GUI_FULLSCREEN_PREVIEW_X + THERMAL_GUI_FULLSCREEN_PREVIEW_WIDTH, 0);
    lv_obj_set_size(right_rail, THERMAL_GUI_SCREEN_WIDTH - THERMAL_GUI_FULLSCREEN_PREVIEW_X - THERMAL_GUI_FULLSCREEN_PREVIEW_WIDTH,
                    THERMAL_GUI_SCREEN_HEIGHT);
    lv_obj_clear_flag(right_rail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(right_rail, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(right_rail, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(right_rail, LV_BORDER_SIDE_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(right_rail, lv_color_hex(THERMAL_GUI_COLOR_BORDER), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(right_rail, lv_color_hex(THERMAL_GUI_COLOR_SURFACE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(right_rail, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(right_rail, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui->WidgetsDemo_fullscreen_preview_frame = lv_obj_create(ui->WidgetsDemo_fullscreen);
    lv_obj_set_pos(ui->WidgetsDemo_fullscreen_preview_frame, THERMAL_GUI_FULLSCREEN_PREVIEW_X, THERMAL_GUI_FULLSCREEN_PREVIEW_Y);
    lv_obj_set_size(ui->WidgetsDemo_fullscreen_preview_frame, THERMAL_GUI_FULLSCREEN_PREVIEW_WIDTH, THERMAL_GUI_FULLSCREEN_PREVIEW_HEIGHT);
    lv_obj_clear_flag(ui->WidgetsDemo_fullscreen_preview_frame, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(ui->WidgetsDemo_fullscreen_preview_frame, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->WidgetsDemo_fullscreen_preview_frame, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->WidgetsDemo_fullscreen_preview_frame, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->WidgetsDemo_fullscreen_preview_frame, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(ui->WidgetsDemo_fullscreen_preview_frame, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->WidgetsDemo_fullscreen_preview_frame, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui->WidgetsDemo_fullscreen_preview_img = lv_img_create(ui->WidgetsDemo_fullscreen_preview_frame);
    lv_obj_set_pos(ui->WidgetsDemo_fullscreen_preview_img, 0, 0);
    lv_obj_set_size(ui->WidgetsDemo_fullscreen_preview_img, THERMAL_GUI_FULLSCREEN_PREVIEW_WIDTH, THERMAL_GUI_FULLSCREEN_PREVIEW_HEIGHT);
    lv_obj_clear_flag(ui->WidgetsDemo_fullscreen_preview_img, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(ui->WidgetsDemo_fullscreen_preview_img, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->WidgetsDemo_fullscreen_preview_img, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui->WidgetsDemo_fullscreen_preview_cross_h = lv_obj_create(ui->WidgetsDemo_fullscreen_preview_frame);
    lv_obj_set_pos(ui->WidgetsDemo_fullscreen_preview_cross_h,
                   (THERMAL_GUI_FULLSCREEN_PREVIEW_WIDTH - THERMAL_GUI_FULLSCREEN_PREVIEW_CROSS_LEN) / 2,
                   THERMAL_GUI_FULLSCREEN_PREVIEW_HEIGHT / 2);
    lv_obj_set_size(ui->WidgetsDemo_fullscreen_preview_cross_h, THERMAL_GUI_FULLSCREEN_PREVIEW_CROSS_LEN, 1);
    lv_obj_set_style_bg_color(ui->WidgetsDemo_fullscreen_preview_cross_h, lv_color_hex(0xfafafa), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->WidgetsDemo_fullscreen_preview_cross_h, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->WidgetsDemo_fullscreen_preview_cross_h, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->WidgetsDemo_fullscreen_preview_cross_h, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui->WidgetsDemo_fullscreen_preview_cross_v = lv_obj_create(ui->WidgetsDemo_fullscreen_preview_frame);
    lv_obj_set_pos(ui->WidgetsDemo_fullscreen_preview_cross_v,
                   THERMAL_GUI_FULLSCREEN_PREVIEW_WIDTH / 2,
                   (THERMAL_GUI_FULLSCREEN_PREVIEW_HEIGHT - THERMAL_GUI_FULLSCREEN_PREVIEW_CROSS_LEN) / 2);
    lv_obj_set_size(ui->WidgetsDemo_fullscreen_preview_cross_v, 1, THERMAL_GUI_FULLSCREEN_PREVIEW_CROSS_LEN);
    lv_obj_set_style_bg_color(ui->WidgetsDemo_fullscreen_preview_cross_v, lv_color_hex(0xfafafa), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->WidgetsDemo_fullscreen_preview_cross_v, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui->WidgetsDemo_fullscreen_preview_cross_v, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->WidgetsDemo_fullscreen_preview_cross_v, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui->WidgetsDemo_fullscreen_preview_center_temp = thermal_gui_create_status_badge(ui->WidgetsDemo_fullscreen_preview_frame, "Center 36.8 C", lv_color_hex(THERMAL_GUI_COLOR_TEXT), 12, 148);
    lv_obj_set_pos(ui->WidgetsDemo_fullscreen_preview_center_temp, 12, 12);
    ui->WidgetsDemo_fullscreen_preview_max_temp = thermal_gui_create_status_badge(ui->WidgetsDemo_fullscreen_preview_frame, "Max 128.4 C", lv_color_hex(THERMAL_GUI_COLOR_HOT), 480, 148);
    lv_obj_set_pos(ui->WidgetsDemo_fullscreen_preview_max_temp, 480, 12);
    ui->WidgetsDemo_fullscreen_preview_min_temp = thermal_gui_create_status_badge(ui->WidgetsDemo_fullscreen_preview_frame, "Min 24.1 C", lv_color_hex(THERMAL_GUI_COLOR_COLD), 12, 148);
    lv_obj_set_pos(ui->WidgetsDemo_fullscreen_preview_min_temp, 12, 436);
    ui->WidgetsDemo_fullscreen_preview_palette_name = thermal_gui_create_status_badge(ui->WidgetsDemo_fullscreen_preview_frame, "锐化", lv_color_hex(THERMAL_GUI_COLOR_ACCENT), 520, 108);
    lv_obj_set_pos(ui->WidgetsDemo_fullscreen_preview_palette_name, 520, 436);

    ui->WidgetsDemo_fullscreen_preview_max_marker = thermal_gui_create_marker_label(ui->WidgetsDemo_fullscreen_preview_frame, "高", 420, 96);

    ui->WidgetsDemo_fullscreen_preview_min_marker = thermal_gui_create_marker_label(ui->WidgetsDemo_fullscreen_preview_frame, "低", 80, 288);

    button = thermal_gui_create_footer_button(ui->WidgetsDemo_fullscreen, "设置", THERMAL_GUI_FULLSCREEN_BUTTON_X, THERMAL_GUI_FULLSCREEN_BUTTON_WIDTH, lv_color_hex(THERMAL_GUI_COLOR_COLD), true);
    lv_obj_set_pos(button, THERMAL_GUI_FULLSCREEN_BUTTON_X, THERMAL_GUI_FULLSCREEN_BUTTON_Y);
    lv_obj_set_size(button, THERMAL_GUI_FULLSCREEN_BUTTON_WIDTH, THERMAL_GUI_FULLSCREEN_BUTTON_HEIGHT);
    ui->WidgetsDemo_fullscreen_btn_settings = button;

    events_init_WidgetsDemoFullscreen(ui);
}
