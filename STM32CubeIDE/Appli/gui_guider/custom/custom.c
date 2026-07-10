// SPDX-License-Identifier: MIT
// Copyright 2020 NXP

#include "custom.h"

#include <stdio.h>
#include <string.h>

#include "app_filex.h"
#include "app_threadx.h"

#define CFG_THERMAL_GUI_SNAPSHOT_MAX_WIDTH       640U
#define CFG_THERMAL_GUI_SNAPSHOT_MAX_HEIGHT      480U
#define CFG_THERMAL_GUI_SNAPSHOT_MAX_PIXELS      (CFG_THERMAL_GUI_SNAPSHOT_MAX_WIDTH * CFG_THERMAL_GUI_SNAPSHOT_MAX_HEIGHT)
#define CFG_THERMAL_GUI_ACCENT_COLOR             0xffb454U
#define CFG_THERMAL_GUI_BG_COLOR                 0x141a1fU
#define CFG_THERMAL_GUI_PANEL_COLOR              0x20262cU
#define CFG_THERMAL_GUI_BORDER_COLOR             0x39424bU

static lv_obj_t *g_snapshot_gallery_screen = NULL;
static lv_obj_t *g_snapshot_gallery_title = NULL;
static lv_obj_t *g_snapshot_gallery_status = NULL;
static lv_obj_t *g_snapshot_gallery_panel = NULL;
static lv_obj_t *g_snapshot_gallery_img = NULL;
static lv_obj_t *g_snapshot_gallery_btn_prev = NULL;
static lv_obj_t *g_snapshot_gallery_btn_next = NULL;
static lv_obj_t *g_snapshot_gallery_btn_back = NULL;
static lv_obj_t *g_snapshot_gallery_btn_delete = NULL;
static lv_obj_t *g_snapshot_entry_button = NULL;
static lv_obj_t *g_snapshot_status_label = NULL;
static lv_obj_t *g_snapshot_hidden_canvas = NULL;
static lv_obj_t *g_snapshot_fullscreen_button = NULL;
static lv_obj_t *g_auto_snapshot_toggle_button = NULL;
static lv_obj_t *g_auto_snapshot_period_label = NULL;
static lv_obj_t *g_auto_snapshot_period_minus_button = NULL;
static lv_obj_t *g_auto_snapshot_period_plus_button = NULL;
static lv_ui *g_snapshot_ui = NULL;
static lv_img_dsc_t g_snapshot_view_img_dsc;
static CHAR g_snapshot_gallery_current_name[CFG_APP_FILEX_SNAPSHOT_NAME_LEN];
static uint32_t g_snapshot_gallery_count = 0U;
static uint32_t g_snapshot_gallery_index = 0U;
static uint32_t g_snapshot_gallery_snapshot_index = 0U;
static uint8_t g_auto_snapshot_enable = 0U;
static uint8_t g_auto_snapshot_period_seconds = 3U;
static uint32_t g_auto_snapshot_last_tick_ms = 0U;
static uint16_t g_snapshot_canvas_rgb565[CFG_THERMAL_GUI_SNAPSHOT_MAX_PIXELS]
  __attribute__((section(".EXTRAM"), aligned(32)));
static uint16_t g_snapshot_view_rgb565[CFG_THERMAL_GUI_SNAPSHOT_MAX_PIXELS]
  __attribute__((section(".EXTRAM"), aligned(32)));

static void thermal_gui_gallery_create_screen(lv_ui *ui);
static void thermal_gui_snapshot_ensure_canvas(void);
static void thermal_gui_auto_snapshot_refresh_controls(void);
static void thermal_gui_snapshot_draw_ai_boxes(lv_obj_t *canvas,
                                               lv_obj_t *image_obj,
                                               uint8_t fullscreen_mode);
static void thermal_gui_snapshot_draw_badge(lv_obj_t *canvas,
                                            lv_coord_t x,
                                            lv_coord_t y,
                                            lv_coord_t width,
                                            lv_coord_t height,
                                            const char *text,
                                            lv_color_t text_color);
static void thermal_gui_snapshot_draw_badge_with_font(lv_obj_t *canvas,
                                                      lv_coord_t x,
                                                      lv_coord_t y,
                                                      lv_coord_t width,
                                                      lv_coord_t height,
                                                      const char *text,
                                                      lv_color_t text_color,
                                                      const lv_font_t *font_ptr);

/**
 * @brief Get the first child label text inside a container object.
 * @param container Container object that owns a label child.
 * @return const char* Label text, or an empty string when unavailable.
 */
static const char *thermal_gui_get_child_label_text(lv_obj_t *container)
{
    lv_obj_t *label_obj;

    if (container == NULL)
    {
        return "";
    }

    label_obj = lv_obj_get_child(container, 0);
    if ((label_obj == NULL) || (lv_obj_check_type(label_obj, &lv_label_class) == false))
    {
        return "";
    }

    return lv_label_get_text(label_obj);
}

/**
 * @brief Replace the first child label text inside a container object.
 * @param container Container object that owns a label child.
 * @param text New label text.
 * @return None.
 */
static void thermal_gui_set_child_label_text(lv_obj_t *container, const char *text)
{
    lv_obj_t *label_obj;

    if ((container == NULL) || (text == NULL))
    {
        return;
    }

    label_obj = lv_obj_get_child(container, 0);
    if ((label_obj != NULL) && (lv_obj_check_type(label_obj, &lv_label_class) != false))
    {
        lv_label_set_text(label_obj, text);
    }
}

/**
 * @brief Update the snapshot status hint shown on the system tab.
 * @param text Status text to display.
 * @return None.
 */
static void thermal_gui_snapshot_set_status(const char *text)
{
    if ((g_snapshot_status_label != NULL) && (text != NULL))
    {
        lv_label_set_text(g_snapshot_status_label, text);
    }
}

/**
 * @brief Create one custom dark action button.
 * @param parent Parent object.
 * @param text Button text.
 * @param x X coordinate relative to the parent.
 * @param y Y coordinate relative to the parent.
 * @param width Button width.
 * @param accent Accent RGB color value.
 * @return lv_obj_t* Created button object.
 */
static lv_obj_t *thermal_gui_create_custom_button(lv_obj_t *parent,
                                                  const char *text,
                                                  lv_coord_t x,
                                                  lv_coord_t y,
                                                  lv_coord_t width,
                                                  uint32_t accent)
{
    lv_obj_t *button;
    lv_obj_t *label_obj;

    if (parent == NULL)
    {
        return NULL;
    }

    button = lv_btn_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, 34);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(button, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x232b31), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(button, lv_color_hex(accent), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(button, lv_color_hex(0xf0f4f7), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    label_obj = lv_label_create(button);
    lv_label_set_text(label_obj, text);
    lv_obj_set_style_text_font(label_obj, &lv_font_thermal_cn_18, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(label_obj);

    return button;
}

/**
 * @brief Refresh the AutoCap control texts and enabled states.
 * @return None.
 */
static void thermal_gui_auto_snapshot_refresh_controls(void)
{
    char text_buffer[24];

    if (g_auto_snapshot_toggle_button != NULL)
    {
        thermal_gui_set_child_label_text(g_auto_snapshot_toggle_button,
                                         (g_auto_snapshot_enable != 0U) ? "AutoCap ON" : "AutoCap OFF");
    }

    if (g_auto_snapshot_period_label != NULL)
    {
        (void)snprintf(text_buffer, sizeof(text_buffer), "%us", (unsigned int)g_auto_snapshot_period_seconds);
        lv_label_set_text(g_auto_snapshot_period_label, text_buffer);
    }

    if (g_auto_snapshot_period_minus_button != NULL)
    {
        if (g_auto_snapshot_period_seconds <= 2U)
        {
            lv_obj_add_state(g_auto_snapshot_period_minus_button, LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_clear_state(g_auto_snapshot_period_minus_button, LV_STATE_DISABLED);
        }
    }

    if (g_auto_snapshot_period_plus_button != NULL)
    {
        if (g_auto_snapshot_period_seconds >= 10U)
        {
            lv_obj_add_state(g_auto_snapshot_period_plus_button, LV_STATE_DISABLED);
        }
        else
        {
            lv_obj_clear_state(g_auto_snapshot_period_plus_button, LV_STATE_DISABLED);
        }
    }
}

/**
 * @brief Draw one compact marker badge at an explicit position.
 * @param canvas Canvas object.
 * @param x Left coordinate.
 * @param y Top coordinate.
 * @param text Marker text.
 * @return None.
 */
static void thermal_gui_snapshot_draw_marker(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, const char *text)
{
    thermal_gui_snapshot_draw_badge_with_font(canvas, x, y, 30, 24, text, lv_color_hex(0xFFFFFF), &lv_font_thermal_cn_18);
}

/**
 * @brief Draw fixed-layout snapshot badges using the current live texts.
 * @param canvas Canvas object.
 * @param center_text Center temperature text.
 * @param max_text Maximum temperature text.
 * @param min_text Minimum temperature text.
 * @param image_width Snapshot image width.
 * @param image_height Snapshot image height.
 * @return None.
 */
static void thermal_gui_snapshot_draw_fixed_badges(lv_obj_t *canvas,
                                                   const char *center_text,
                                                   const char *max_text,
                                                   const char *min_text,
                                                   uint16_t image_width,
                                                   uint16_t image_height)
{
    lv_coord_t badge_width;
    lv_coord_t bottom_y;

    badge_width = (image_width >= 480U) ? 200 : 156;
    bottom_y = (image_height > 40U) ? (lv_coord_t)(image_height - 34U) : 0;

    thermal_gui_snapshot_draw_badge(canvas, 8, 8, badge_width, 26, center_text, lv_color_hex(0xFFFFFF));
    thermal_gui_snapshot_draw_badge(canvas,
                                    (lv_coord_t)(image_width - badge_width - 8),
                                    8,
                                    badge_width,
                                    26,
                                    max_text,
                                    lv_color_hex(0xFFD966));
    thermal_gui_snapshot_draw_badge(canvas,
                                    (lv_coord_t)(image_width - badge_width - 8),
                                    bottom_y,
                                    badge_width,
                                    26,
                                    min_text,
                                    lv_color_hex(0x8FD3FF));
}

/**
 * @brief Draw one badge rectangle with centered text onto the snapshot canvas.
 * @param canvas Canvas object.
 * @param x Left coordinate on the snapshot image.
 * @param y Top coordinate on the snapshot image.
 * @param width Badge width.
 * @param height Badge height.
 * @param text Text to draw.
 * @param text_color RGB565 text color.
 * @return None.
 */
static void thermal_gui_snapshot_draw_badge(lv_obj_t *canvas,
                                            lv_coord_t x,
                                            lv_coord_t y,
                                            lv_coord_t width,
                                            lv_coord_t height,
                                            const char *text,
                                            lv_color_t text_color)
{
    thermal_gui_snapshot_draw_badge_with_font(canvas, x, y, width, height, text, text_color, &lv_font_montserratMedium_19);
}

/**
 * @brief Draw one badge rectangle with centered text onto the snapshot canvas.
 * @param canvas Canvas object.
 * @param x Left coordinate on the snapshot image.
 * @param y Top coordinate on the snapshot image.
 * @param width Badge width.
 * @param height Badge height.
 * @param text Text to draw.
 * @param text_color RGB565 text color.
 * @param font_ptr Font used to draw the text.
 * @return None.
 */
static void thermal_gui_snapshot_draw_badge_with_font(lv_obj_t *canvas,
                                                      lv_coord_t x,
                                                      lv_coord_t y,
                                                      lv_coord_t width,
                                                      lv_coord_t height,
                                                      const char *text,
                                                      lv_color_t text_color,
                                                      const lv_font_t *font_ptr)
{
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_label_dsc_t label_dsc;

    if ((canvas == NULL) || (text == NULL) || (font_ptr == NULL) || (width <= 0) || (height <= 0))
    {
        return;
    }

    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_color = lv_color_hex(0x000000);
    rect_dsc.bg_opa = LV_OPA_80;
    rect_dsc.border_width = 1;
    rect_dsc.border_color = lv_color_hex(0x505a63);
    rect_dsc.border_opa = LV_OPA_80;
    rect_dsc.radius = 8;
    lv_canvas_draw_rect(canvas, x, y, width, height, &rect_dsc);

    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = text_color;
    label_dsc.font = font_ptr;
    label_dsc.align = LV_TEXT_ALIGN_CENTER;
    lv_canvas_draw_text(canvas, x + 6, y + 5, width - 12, &label_dsc, text);
}

/**
 * @brief Draw one cross overlay that matches the current GUI switch state.
 * @param canvas Canvas object.
 * @param cross_h Horizontal cross object.
 * @param cross_v Vertical cross object.
 * @param image_width Snapshot image width.
 * @param image_height Snapshot image height.
 * @return None.
 */
static void thermal_gui_snapshot_draw_cross(lv_obj_t *canvas,
                                            lv_obj_t *cross_h,
                                            lv_obj_t *cross_v,
                                            uint16_t image_width,
                                            uint16_t image_height)
{
    lv_draw_line_dsc_t line_dsc;
    lv_point_t line_points[2];
    lv_coord_t center_x;
    lv_coord_t center_y;
    lv_coord_t half_h;
    lv_coord_t half_v;

    if ((canvas == NULL) || (cross_h == NULL) || (cross_v == NULL))
    {
        return;
    }

    if ((lv_obj_has_flag(cross_h, LV_OBJ_FLAG_HIDDEN) != false) ||
        (lv_obj_has_flag(cross_v, LV_OBJ_FLAG_HIDDEN) != false))
    {
        return;
    }

    center_x = (lv_coord_t)(image_width / 2U);
    center_y = (lv_coord_t)(image_height / 2U);
    half_h = (lv_coord_t)(lv_obj_get_width(cross_h) / 2);
    half_v = (lv_coord_t)(lv_obj_get_height(cross_v) / 2);

    lv_draw_line_dsc_init(&line_dsc);
    line_dsc.color = lv_color_hex(0xFFFFFF);
    line_dsc.width = 2;
    line_dsc.opa = LV_OPA_COVER;

    line_points[0].x = center_x - half_h;
    line_points[0].y = center_y;
    line_points[1].x = center_x + half_h;
    line_points[1].y = center_y;
    lv_canvas_draw_line(canvas, line_points, 2U, &line_dsc);

    line_points[0].x = center_x;
    line_points[0].y = center_y - half_v;
    line_points[1].x = center_x;
    line_points[1].y = center_y + half_v;
    lv_canvas_draw_line(canvas, line_points, 2U, &line_dsc);
}

/**
 * @brief Draw one compact marker overlay from the current GUI object state.
 * @param canvas Canvas object.
 * @param image_obj Base image object used as the origin.
 * @param marker_obj Marker object.
 * @return None.
 */
static void thermal_gui_snapshot_draw_marker_from_obj(lv_obj_t *canvas,
                                                      lv_obj_t *image_obj,
                                                      lv_obj_t *marker_obj,
                                                      const char *marker_text)
{
    lv_coord_t local_x;
    lv_coord_t local_y;
    lv_coord_t max_x;
    lv_coord_t max_y;

    if ((canvas == NULL) || (image_obj == NULL) || (marker_obj == NULL) || (marker_text == NULL))
    {
        return;
    }

    if (lv_obj_has_flag(marker_obj, LV_OBJ_FLAG_HIDDEN) != false)
    {
        return;
    }

    local_x = lv_obj_get_x(marker_obj) - lv_obj_get_x(image_obj);
    local_y = lv_obj_get_y(marker_obj) - lv_obj_get_y(image_obj);
    max_x = (lv_coord_t)lv_obj_get_width(image_obj) - 30;
    max_y = (lv_coord_t)lv_obj_get_height(image_obj) - 24;
    if (local_x < 0)
    {
        local_x = 0;
    }
    if (local_y < 0)
    {
        local_y = 0;
    }
    if (local_x > max_x)
    {
        local_x = max_x;
    }
    if (local_y > max_y)
    {
        local_y = max_y;
    }
    thermal_gui_snapshot_draw_marker(canvas, local_x, local_y, marker_text);
}

/**
 * @brief Draw currently visible AI boxes onto the snapshot canvas.
 * @param canvas Canvas object.
 * @param image_obj Base preview image object used as the local origin.
 * @param fullscreen_mode Non-zero selects full-screen overlay boxes.
 * @return None.
 */
static void thermal_gui_snapshot_draw_ai_boxes(lv_obj_t *canvas,
                                               lv_obj_t *image_obj,
                                               uint8_t fullscreen_mode)
{
    app_thermal_ai_snapshot_box_t box_list[CFG_APP_THERMAL_AI_SNAPSHOT_MAX_BOXES];
    uint32_t box_count;
    uint32_t box_index;
    lv_coord_t image_width;
    lv_coord_t image_height;
    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_label_dsc_t label_dsc;

    if ((canvas == NULL) || (image_obj == NULL))
    {
        return;
    }

    image_width = lv_obj_get_width(image_obj);
    image_height = lv_obj_get_height(image_obj);
    box_count = app_thermal_ai_snapshot_collect_boxes(fullscreen_mode,
                                                      box_list,
                                                      CFG_APP_THERMAL_AI_SNAPSHOT_MAX_BOXES);
    for (box_index = 0U; box_index < box_count; box_index++)
    {
        lv_coord_t local_x;
        lv_coord_t local_y;
        lv_coord_t box_w;
        lv_coord_t box_h;

        if (box_list[box_index].valid == 0U)
        {
            continue;
        }

        local_x = (lv_coord_t)((int32_t)box_list[box_index].x - lv_obj_get_x(image_obj));
        local_y = (lv_coord_t)((int32_t)box_list[box_index].y - lv_obj_get_y(image_obj));
        box_w = (lv_coord_t)box_list[box_index].width;
        box_h = (lv_coord_t)box_list[box_index].height;

        if (local_x < 0)
        {
            box_w = (lv_coord_t)((box_w > (lv_coord_t)(-local_x)) ? (box_w + local_x) : 0);
            local_x = 0;
        }
        if (local_y < 0)
        {
            box_h = (lv_coord_t)((box_h > (lv_coord_t)(-local_y)) ? (box_h + local_y) : 0);
            local_y = 0;
        }
        if ((local_x >= image_width) || (local_y >= image_height) || (box_w <= 0) || (box_h <= 0))
        {
            continue;
        }
        if ((local_x + box_w) > image_width)
        {
            box_w = (lv_coord_t)(image_width - local_x);
        }
        if ((local_y + box_h) > image_height)
        {
            box_h = (lv_coord_t)(image_height - local_y);
        }
        if ((box_w <= 0) || (box_h <= 0))
        {
            continue;
        }

        lv_draw_rect_dsc_init(&rect_dsc);
        rect_dsc.bg_opa = LV_OPA_TRANSP;
        rect_dsc.border_width = 3;
        rect_dsc.border_opa = LV_OPA_COVER;
        rect_dsc.border_color = lv_color_hex(box_list[box_index].border_color_rgb888);
        rect_dsc.radius = 0;
        lv_canvas_draw_rect(canvas, local_x, local_y, box_w, box_h, &rect_dsc);

        if (box_list[box_index].label_text[0] != '\0')
        {
            lv_draw_rect_dsc_t label_bg_dsc;
            lv_coord_t label_y = (local_y >= 22) ? (lv_coord_t)(local_y - 22) : local_y;

            lv_draw_rect_dsc_init(&label_bg_dsc);
            label_bg_dsc.bg_opa = LV_OPA_60;
            label_bg_dsc.bg_color = lv_color_black();
            label_bg_dsc.border_width = 1;
            label_bg_dsc.border_opa = LV_OPA_COVER;
            label_bg_dsc.border_color = lv_color_hex(box_list[box_index].border_color_rgb888);
            label_bg_dsc.radius = 4;
            lv_canvas_draw_rect(canvas, local_x, label_y, 84, 20, &label_bg_dsc);

            lv_draw_label_dsc_init(&label_dsc);
            label_dsc.color = lv_color_white();
            label_dsc.font = &lv_font_montserratMedium_19;
            label_dsc.align = LV_TEXT_ALIGN_LEFT;
            lv_canvas_draw_text(canvas, local_x + 4, label_y + 2, 76, &label_dsc, box_list[box_index].label_text);
        }
    }
}

/**
 * @brief Draw all supported overlays onto the snapshot canvas.
 * @param canvas Canvas object.
 * @param image_obj Base image object.
 * @param cross_h Horizontal cross object.
 * @param cross_v Vertical cross object.
 * @param center_badge Center temperature badge.
 * @param max_badge Maximum temperature badge.
 * @param min_badge Minimum temperature badge.
 * @param max_marker Maximum point marker.
 * @param min_marker Minimum point marker.
 * @param image_width Snapshot image width.
 * @param image_height Snapshot image height.
 * @return None.
 */
static void thermal_gui_snapshot_draw_overlays(lv_obj_t *canvas,
                                               lv_obj_t *image_obj,
                                               lv_obj_t *cross_h,
                                               lv_obj_t *cross_v,
                                               lv_obj_t *center_badge,
                                               lv_obj_t *max_badge,
                                               lv_obj_t *min_badge,
                                               lv_obj_t *max_marker,
                                               lv_obj_t *min_marker,
                                               uint8_t fullscreen_mode,
                                               uint16_t image_width,
                                               uint16_t image_height)
{
    thermal_gui_snapshot_draw_cross(canvas, cross_h, cross_v, image_width, image_height);
    thermal_gui_snapshot_draw_fixed_badges(canvas,
                                           thermal_gui_get_child_label_text(center_badge),
                                           thermal_gui_get_child_label_text(max_badge),
                                           thermal_gui_get_child_label_text(min_badge),
                                           image_width,
                                           image_height);
    thermal_gui_snapshot_draw_ai_boxes(canvas, image_obj, fullscreen_mode);
    thermal_gui_snapshot_draw_marker_from_obj(canvas, image_obj, max_marker, "高");
    thermal_gui_snapshot_draw_marker_from_obj(canvas, image_obj, min_marker, "低");
}

/**
 * @brief Capture one thermal image object and save it to FileX storage.
 * @param image_obj Thermal image object.
 * @param cross_h Horizontal cross object.
 * @param cross_v Vertical cross object.
 * @param center_badge Center temperature badge.
 * @param max_badge Maximum temperature badge.
 * @param min_badge Minimum temperature badge.
 * @param max_marker Maximum point marker.
 * @param min_marker Minimum point marker.
 * @param saved_name_ptr Optional output file-name buffer.
 * @param saved_name_size Size of @p saved_name_ptr in bytes.
 * @return UINT FileX status code.
 */
static UINT thermal_gui_snapshot_capture_from_objects(lv_obj_t *image_obj,
                                                      lv_obj_t *cross_h,
                                                      lv_obj_t *cross_v,
                                                      lv_obj_t *center_badge,
                                                      lv_obj_t *max_badge,
                                                      lv_obj_t *min_badge,
                                                      lv_obj_t *max_marker,
                                                      lv_obj_t *min_marker,
                                                      uint8_t fullscreen_mode,
                                                      CHAR *saved_name_ptr,
                                                      uint32_t saved_name_size)
{
    const void *src_ptr;
    const lv_img_dsc_t *img_dsc_ptr;
    uint16_t image_width;
    uint16_t image_height;

    if (image_obj == NULL)
    {
        return FX_PTR_ERROR;
    }

    thermal_gui_snapshot_ensure_canvas();
    if (g_snapshot_hidden_canvas == NULL)
    {
        return FX_IO_ERROR;
    }

    src_ptr = lv_img_get_src(image_obj);
    if ((src_ptr == NULL) || (lv_img_src_get_type(src_ptr) != LV_IMG_SRC_VARIABLE))
    {
        return FX_NOT_OPEN;
    }

    img_dsc_ptr = (const lv_img_dsc_t *)src_ptr;
    image_width = (uint16_t)img_dsc_ptr->header.w;
    image_height = (uint16_t)img_dsc_ptr->header.h;
    if (((uint32_t)image_width * (uint32_t)image_height) > CFG_THERMAL_GUI_SNAPSHOT_MAX_PIXELS)
    {
        return FX_BUFFER_ERROR;
    }

    (void)memcpy(g_snapshot_canvas_rgb565, img_dsc_ptr->data, img_dsc_ptr->data_size);
    lv_canvas_set_buffer(g_snapshot_hidden_canvas,
                         g_snapshot_canvas_rgb565,
                         image_width,
                         image_height,
                         LV_IMG_CF_TRUE_COLOR);
    thermal_gui_snapshot_draw_overlays(g_snapshot_hidden_canvas,
                                       image_obj,
                                       cross_h,
                                       cross_v,
                                       center_badge,
                                       max_badge,
                                       min_badge,
                                       max_marker,
                                       min_marker,
                                       fullscreen_mode,
                                       image_width,
                                       image_height);

    return app_filex_snapshot_save_rgb565(g_snapshot_canvas_rgb565,
                                          image_width,
                                          image_height,
                                          saved_name_ptr,
                                          saved_name_size);
}

/**
 * @brief Capture the current normal preview and save it to FileX storage.
 * @param ui UI context.
 * @param saved_name_ptr Optional output file-name buffer.
 * @param saved_name_size Size of @p saved_name_ptr in bytes.
 * @return UINT FileX status code.
 */
static UINT thermal_gui_snapshot_capture_preview(lv_ui *ui, CHAR *saved_name_ptr, uint32_t saved_name_size)
{
    if (ui == NULL)
    {
        return FX_PTR_ERROR;
    }

    return thermal_gui_snapshot_capture_from_objects(ui->WidgetsDemo_preview_img,
                                                     ui->WidgetsDemo_preview_cross_h,
                                                     ui->WidgetsDemo_preview_cross_v,
                                                     ui->WidgetsDemo_preview_center_temp,
                                                     ui->WidgetsDemo_preview_max_temp,
                                                     ui->WidgetsDemo_preview_min_temp,
                                                     ui->WidgetsDemo_preview_max_marker,
                                                     ui->WidgetsDemo_preview_min_marker,
                                                     0U,
                                                     saved_name_ptr,
                                                     saved_name_size);
}

/**
 * @brief Capture the full-screen preview and save it to FileX storage.
 * @param ui UI context.
 * @param saved_name_ptr Optional output file-name buffer.
 * @param saved_name_size Size of @p saved_name_ptr in bytes.
 * @return UINT FileX status code.
 */
static UINT thermal_gui_snapshot_capture_fullscreen(lv_ui *ui, CHAR *saved_name_ptr, uint32_t saved_name_size)
{
    if (ui == NULL)
    {
        return FX_PTR_ERROR;
    }

    return thermal_gui_snapshot_capture_from_objects(ui->WidgetsDemo_fullscreen_preview_img,
                                                     ui->WidgetsDemo_fullscreen_preview_cross_h,
                                                     ui->WidgetsDemo_fullscreen_preview_cross_v,
                                                     ui->WidgetsDemo_fullscreen_preview_center_temp,
                                                     ui->WidgetsDemo_fullscreen_preview_max_temp,
                                                     ui->WidgetsDemo_fullscreen_preview_min_temp,
                                                     ui->WidgetsDemo_fullscreen_preview_max_marker,
                                                     ui->WidgetsDemo_fullscreen_preview_min_marker,
                                                     1U,
                                                     saved_name_ptr,
                                                     saved_name_size);
}

/**
 * @brief Refresh the in-memory gallery name list from FileX storage.
 * @return UINT FileX status code.
 */
static UINT thermal_gui_gallery_reload_list(void)
{
    UINT status;

    status = app_filex_snapshot_get_latest(g_snapshot_gallery_current_name,
                                           sizeof(g_snapshot_gallery_current_name),
                                           &g_snapshot_gallery_count,
                                           &g_snapshot_gallery_snapshot_index);
    if (status == FX_SUCCESS)
    {
        g_snapshot_gallery_index = g_snapshot_gallery_count - 1U;
    }
    else
    {
        g_snapshot_gallery_current_name[0] = '\0';
        g_snapshot_gallery_count = 0U;
        g_snapshot_gallery_index = 0U;
        g_snapshot_gallery_snapshot_index = 0U;
    }

    return status;
}

/**
 * @brief Ensure the hidden snapshot canvas exists before capturing overlays.
 * @return None.
 */
static void thermal_gui_snapshot_ensure_canvas(void)
{
    if (g_snapshot_hidden_canvas != NULL)
    {
        return;
    }

    g_snapshot_hidden_canvas = lv_canvas_create(lv_layer_top());
    lv_obj_add_flag(g_snapshot_hidden_canvas, LV_OBJ_FLAG_HIDDEN);
    lv_canvas_set_buffer(g_snapshot_hidden_canvas,
                         g_snapshot_canvas_rgb565,
                         CFG_THERMAL_GUI_SNAPSHOT_MAX_WIDTH,
                         CFG_THERMAL_GUI_SNAPSHOT_MAX_HEIGHT,
                         LV_IMG_CF_TRUE_COLOR);
}

/**
 * @brief Update the enabled state of the gallery navigation buttons.
 * @return None.
 */
static void thermal_gui_gallery_refresh_nav_buttons(void)
{
    if ((g_snapshot_gallery_btn_prev == NULL) || (g_snapshot_gallery_btn_next == NULL))
    {
        return;
    }

    if ((g_snapshot_gallery_count == 0U) || (g_snapshot_gallery_index == 0U))
    {
        lv_obj_add_state(g_snapshot_gallery_btn_prev, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_clear_state(g_snapshot_gallery_btn_prev, LV_STATE_DISABLED);
    }

    if ((g_snapshot_gallery_count == 0U) || ((g_snapshot_gallery_index + 1U) >= g_snapshot_gallery_count))
    {
        lv_obj_add_state(g_snapshot_gallery_btn_next, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_clear_state(g_snapshot_gallery_btn_next, LV_STATE_DISABLED);
    }
}

/**
 * @brief Load one gallery entry into the image viewer screen.
 * @param entry_index Zero-based gallery entry index.
 * @return UINT FileX status code.
 */
static UINT thermal_gui_gallery_load_entry(uint32_t entry_index)
{
    UINT status;
    uint16_t image_width = 0U;
    uint16_t image_height = 0U;
    char title_text[64];
    uint32_t zoom_value;
    uint32_t zoom_x;
    uint32_t zoom_y;

    (void)entry_index;

    if ((g_snapshot_gallery_count == 0U) || (g_snapshot_gallery_current_name[0] == '\0'))
    {
        lv_label_set_text(g_snapshot_gallery_status, "No snapshot saved");
        lv_obj_add_flag(g_snapshot_gallery_img, LV_OBJ_FLAG_HIDDEN);
        thermal_gui_gallery_refresh_nav_buttons();
        return FX_NO_MORE_ENTRIES;
    }

    status = app_filex_snapshot_load_rgb565(g_snapshot_gallery_current_name,
                                            g_snapshot_view_rgb565,
                                            CFG_THERMAL_GUI_SNAPSHOT_MAX_PIXELS,
                                            &image_width,
                                            &image_height);
    if (status != FX_SUCCESS)
    {
        (void)snprintf(title_text, sizeof(title_text), "Load failed: %s", app_filex_status_to_string(status));
        lv_label_set_text(g_snapshot_gallery_status, title_text);
        lv_obj_add_flag(g_snapshot_gallery_img, LV_OBJ_FLAG_HIDDEN);
        thermal_gui_gallery_refresh_nav_buttons();
        return status;
    }

    g_snapshot_view_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    g_snapshot_view_img_dsc.header.always_zero = 0U;
    g_snapshot_view_img_dsc.header.w = image_width;
    g_snapshot_view_img_dsc.header.h = image_height;
    g_snapshot_view_img_dsc.data_size = (uint32_t)image_width * (uint32_t)image_height * sizeof(uint16_t);
    g_snapshot_view_img_dsc.data = (const uint8_t *)g_snapshot_view_rgb565;

    lv_img_set_src(g_snapshot_gallery_img, &g_snapshot_view_img_dsc);
    zoom_x = ((uint32_t)lv_obj_get_width(g_snapshot_gallery_panel) * 256U) / image_width;
    zoom_y = ((uint32_t)lv_obj_get_height(g_snapshot_gallery_panel) * 256U) / image_height;
    zoom_value = (zoom_x < zoom_y) ? zoom_x : zoom_y;
    if (zoom_value > 256U)
    {
        zoom_value = 256U;
    }
    lv_img_set_zoom(g_snapshot_gallery_img, (uint16_t)zoom_value);
    lv_obj_center(g_snapshot_gallery_img);
    lv_obj_clear_flag(g_snapshot_gallery_img, LV_OBJ_FLAG_HIDDEN);

    (void)snprintf(title_text,
                   sizeof(title_text),
                   "%s  (%lu/%lu)",
                   g_snapshot_gallery_current_name,
                   (unsigned long)(g_snapshot_gallery_index + 1U),
                   (unsigned long)g_snapshot_gallery_count);
    lv_label_set_text(g_snapshot_gallery_status, title_text);
    thermal_gui_gallery_refresh_nav_buttons();

    return FX_SUCCESS;
}

/**
 * @brief Open the gallery screen and display the newest snapshot.
 * @param ui UI context.
 * @return None.
 */
static void thermal_gui_gallery_open(lv_ui *ui)
{
    UINT status;

    if ((ui == NULL) || (g_snapshot_gallery_screen == NULL))
    {
        thermal_gui_gallery_create_screen(ui);
        if (g_snapshot_gallery_screen == NULL)
        {
            return;
        }
    }

    status = thermal_gui_gallery_reload_list();
    if (status != FX_SUCCESS)
    {
        lv_label_set_text(g_snapshot_gallery_status, app_filex_status_to_string(status));
        lv_obj_add_flag(g_snapshot_gallery_img, LV_OBJ_FLAG_HIDDEN);
        g_snapshot_gallery_count = 0U;
    }
    else if (g_snapshot_gallery_count > 0U)
    {
        (void)thermal_gui_gallery_load_entry(g_snapshot_gallery_count - 1U);
    }
    else
    {
        lv_label_set_text(g_snapshot_gallery_status, "No snapshot saved");
        lv_obj_add_flag(g_snapshot_gallery_img, LV_OBJ_FLAG_HIDDEN);
        thermal_gui_gallery_refresh_nav_buttons();
    }

    lv_scr_load(g_snapshot_gallery_screen);
}

/**
 * @brief Return from the gallery screen to the thermal settings page.
 * @param ui UI context.
 * @return None.
 */
static void thermal_gui_gallery_close(lv_ui *ui)
{
    if (ui != NULL)
    {
        lv_scr_load(ui->WidgetsDemo);
    }
}

/**
 * @brief Handle the footer snapshot save button.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_snapshot_save_event_cb(lv_event_t *e)
{
    CHAR saved_name[CFG_APP_FILEX_SNAPSHOT_NAME_LEN];
    UINT status;

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    saved_name[0] = '\0';
    status = thermal_gui_snapshot_capture_preview((lv_ui *)lv_event_get_user_data(e),
                                                  saved_name,
                                                  sizeof(saved_name));
    if (status == FX_SUCCESS)
    {
        char status_text[48];

        (void)snprintf(status_text, sizeof(status_text), "Saved %s", saved_name);
        thermal_gui_snapshot_set_status(status_text);
    }
    else
    {
        char status_text[48];

        (void)snprintf(status_text, sizeof(status_text), "Save failed: %s", app_filex_status_to_string(status));
        thermal_gui_snapshot_set_status(status_text);
    }
}

/**
 * @brief Handle the full-screen snapshot save button.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_snapshot_save_fullscreen_event_cb(lv_event_t *e)
{
    CHAR saved_name[CFG_APP_FILEX_SNAPSHOT_NAME_LEN];
    UINT status;
    char status_text[48];

    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    saved_name[0] = '\0';
    status = thermal_gui_snapshot_capture_fullscreen((lv_ui *)lv_event_get_user_data(e),
                                                     saved_name,
                                                     sizeof(saved_name));
    if (status == FX_SUCCESS)
    {
        (void)snprintf(status_text, sizeof(status_text), "Saved %s", saved_name);
    }
    else
    {
        (void)snprintf(status_text, sizeof(status_text), "Save failed: %s", app_filex_status_to_string(status));
    }

    thermal_gui_snapshot_set_status(status_text);
    if (g_snapshot_gallery_status != NULL)
    {
        lv_label_set_text(g_snapshot_gallery_status, status_text);
    }
}

/**
 * @brief Handle the AutoCap toggle button.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_auto_snapshot_toggle_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED)
    {
        return;
    }

    g_auto_snapshot_enable = (g_auto_snapshot_enable == 0U) ? 1U : 0U;
    g_auto_snapshot_last_tick_ms = 0U;
    thermal_gui_auto_snapshot_refresh_controls();
    thermal_gui_snapshot_set_status((g_auto_snapshot_enable != 0U) ? "AutoCap enabled" : "AutoCap disabled");
}

/**
 * @brief Handle the AutoCap period decrement button.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_auto_snapshot_period_minus_event_cb(lv_event_t *e)
{
    if ((lv_event_get_code(e) != LV_EVENT_CLICKED) || (g_auto_snapshot_period_seconds <= 2U))
    {
        return;
    }

    g_auto_snapshot_period_seconds--;
    thermal_gui_auto_snapshot_refresh_controls();
}

/**
 * @brief Handle the AutoCap period increment button.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_auto_snapshot_period_plus_event_cb(lv_event_t *e)
{
    if ((lv_event_get_code(e) != LV_EVENT_CLICKED) || (g_auto_snapshot_period_seconds >= 10U))
    {
        return;
    }

    g_auto_snapshot_period_seconds++;
    thermal_gui_auto_snapshot_refresh_controls();
}

/**
 * @brief Handle taps on the gallery entry card inside the system tab.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_gallery_open_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        app_uart_request_file_mode(APP_UART_FILE_HOLD_GUI);
        thermal_gui_snapshot_set_status("图库模式，串口流已暂停");
        thermal_gui_gallery_open((lv_ui *)lv_event_get_user_data(e));
    }
}

/**
 * @brief Handle the gallery back button.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_gallery_back_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED)
    {
        app_uart_release_file_mode(APP_UART_FILE_HOLD_GUI);
        thermal_gui_snapshot_set_status("已退出图库，串口流恢复");
        thermal_gui_gallery_close((lv_ui *)lv_event_get_user_data(e));
    }
}

/**
 * @brief Handle the gallery previous button.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_gallery_prev_event_cb(lv_event_t *e)
{
    UINT status;

    if ((lv_event_get_code(e) == LV_EVENT_CLICKED) && (g_snapshot_gallery_index > 0U))
    {
        status = app_filex_snapshot_get_adjacent(g_snapshot_gallery_snapshot_index,
                                                 0U,
                                                 g_snapshot_gallery_current_name,
                                                 sizeof(g_snapshot_gallery_current_name),
                                                 &g_snapshot_gallery_count,
                                                 &g_snapshot_gallery_index,
                                                 &g_snapshot_gallery_snapshot_index);
        if (status == FX_SUCCESS)
        {
            (void)thermal_gui_gallery_load_entry(g_snapshot_gallery_index);
        }
        else
        {
            thermal_gui_gallery_refresh_nav_buttons();
        }
    }
}

/**
 * @brief Handle the gallery next button.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_gallery_next_event_cb(lv_event_t *e)
{
    UINT status;

    if ((lv_event_get_code(e) == LV_EVENT_CLICKED) &&
        ((g_snapshot_gallery_index + 1U) < g_snapshot_gallery_count))
    {
        status = app_filex_snapshot_get_adjacent(g_snapshot_gallery_snapshot_index,
                                                 1U,
                                                 g_snapshot_gallery_current_name,
                                                 sizeof(g_snapshot_gallery_current_name),
                                                 &g_snapshot_gallery_count,
                                                 &g_snapshot_gallery_index,
                                                 &g_snapshot_gallery_snapshot_index);
        if (status == FX_SUCCESS)
        {
            (void)thermal_gui_gallery_load_entry(g_snapshot_gallery_index);
        }
        else
        {
            thermal_gui_gallery_refresh_nav_buttons();
        }
    }
}

/**
 * @brief Handle the gallery delete button.
 * @param e LVGL event descriptor.
 * @return None.
 */
static void thermal_gui_gallery_delete_event_cb(lv_event_t *e)
{
    UINT status;
    UINT next_status;
    uint32_t deleted_snapshot_index;
    char status_text[64];

    if ((lv_event_get_code(e) != LV_EVENT_CLICKED) || (g_snapshot_gallery_count == 0U) ||
        (g_snapshot_gallery_current_name[0] == '\0'))
    {
        return;
    }

    deleted_snapshot_index = g_snapshot_gallery_snapshot_index;
    status = app_filex_snapshot_delete(g_snapshot_gallery_current_name);
    if (status != FX_SUCCESS)
    {
        (void)snprintf(status_text, sizeof(status_text), "Delete failed: %s", app_filex_status_to_string(status));
        lv_label_set_text(g_snapshot_gallery_status, status_text);
        return;
    }

    next_status = app_filex_snapshot_get_adjacent(deleted_snapshot_index,
                                                  1U,
                                                  g_snapshot_gallery_current_name,
                                                  sizeof(g_snapshot_gallery_current_name),
                                                  &g_snapshot_gallery_count,
                                                  &g_snapshot_gallery_index,
                                                  &g_snapshot_gallery_snapshot_index);
    if (next_status != FX_SUCCESS)
    {
        next_status = app_filex_snapshot_get_adjacent(deleted_snapshot_index,
                                                      0U,
                                                      g_snapshot_gallery_current_name,
                                                      sizeof(g_snapshot_gallery_current_name),
                                                      &g_snapshot_gallery_count,
                                                      &g_snapshot_gallery_index,
                                                      &g_snapshot_gallery_snapshot_index);
    }

    if ((next_status == FX_SUCCESS) && (g_snapshot_gallery_count > 0U))
    {
        (void)thermal_gui_gallery_load_entry(g_snapshot_gallery_index);
    }
    else
    {
        lv_label_set_text(g_snapshot_gallery_status, "No snapshot saved");
        lv_obj_add_flag(g_snapshot_gallery_img, LV_OBJ_FLAG_HIDDEN);
        g_snapshot_gallery_current_name[0] = '\0';
        g_snapshot_gallery_count = 0U;
        g_snapshot_gallery_index = 0U;
        g_snapshot_gallery_snapshot_index = 0U;
        thermal_gui_gallery_refresh_nav_buttons();
    }

    thermal_gui_snapshot_set_status("Deleted current snapshot");
}

/**
 * @brief Build the custom gallery viewer screen.
 * @param ui UI context.
 * @return None.
 */
static void thermal_gui_gallery_create_screen(lv_ui *ui)
{
    if (ui == NULL)
    {
        return;
    }

    if (g_snapshot_gallery_screen != NULL)
    {
        return;
    }

    g_snapshot_gallery_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(g_snapshot_gallery_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(g_snapshot_gallery_screen, lv_color_hex(CFG_THERMAL_GUI_BG_COLOR), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(g_snapshot_gallery_screen, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    g_snapshot_gallery_title = lv_label_create(g_snapshot_gallery_screen);
    lv_label_set_text(g_snapshot_gallery_title, "Thermal Gallery");
    lv_obj_set_pos(g_snapshot_gallery_title, 24, 18);
    lv_obj_set_style_text_font(g_snapshot_gallery_title, &lv_font_montserratMedium_32, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(g_snapshot_gallery_title, lv_color_hex(0xf0f4f7), LV_PART_MAIN | LV_STATE_DEFAULT);

    g_snapshot_gallery_status = lv_label_create(g_snapshot_gallery_screen);
    lv_label_set_text(g_snapshot_gallery_status, "Loading...");
    lv_obj_set_pos(g_snapshot_gallery_status, 26, 60);
    lv_obj_set_width(g_snapshot_gallery_status, 520);
    lv_obj_set_style_text_font(g_snapshot_gallery_status, &lv_font_montserratMedium_19, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(g_snapshot_gallery_status, lv_color_hex(0xb9c5cf), LV_PART_MAIN | LV_STATE_DEFAULT);

    g_snapshot_gallery_panel = lv_obj_create(g_snapshot_gallery_screen);
    lv_obj_set_pos(g_snapshot_gallery_panel, 28, 92);
    lv_obj_set_size(g_snapshot_gallery_panel, 682, 360);
    lv_obj_clear_flag(g_snapshot_gallery_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(g_snapshot_gallery_panel, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(g_snapshot_gallery_panel, lv_color_hex(CFG_THERMAL_GUI_PANEL_COLOR), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(g_snapshot_gallery_panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(g_snapshot_gallery_panel, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(g_snapshot_gallery_panel, lv_color_hex(CFG_THERMAL_GUI_BORDER_COLOR), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(g_snapshot_gallery_panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    g_snapshot_gallery_img = lv_img_create(g_snapshot_gallery_panel);
    lv_obj_add_flag(g_snapshot_gallery_img, LV_OBJ_FLAG_HIDDEN);

    g_snapshot_gallery_btn_back = thermal_gui_create_custom_button(g_snapshot_gallery_screen, "Back", 728, 92, 52, 0x7bc6ffU);
    g_snapshot_gallery_btn_prev = thermal_gui_create_custom_button(g_snapshot_gallery_screen, "Prev", 728, 150, 52, 0x4db6ffU);
    g_snapshot_gallery_btn_next = thermal_gui_create_custom_button(g_snapshot_gallery_screen, "Next", 728, 208, 52, CFG_THERMAL_GUI_ACCENT_COLOR);
    g_snapshot_gallery_btn_delete = thermal_gui_create_custom_button(g_snapshot_gallery_screen, "Delete", 720, 266, 60, 0xff6b6bU);

    lv_obj_add_event_cb(g_snapshot_gallery_btn_back, thermal_gui_gallery_back_event_cb, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(g_snapshot_gallery_btn_prev, thermal_gui_gallery_prev_event_cb, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(g_snapshot_gallery_btn_next, thermal_gui_gallery_next_event_cb, LV_EVENT_CLICKED, ui);
    lv_obj_add_event_cb(g_snapshot_gallery_btn_delete, thermal_gui_gallery_delete_event_cb, LV_EVENT_CLICKED, ui);
}

/**
 * @brief Extend the existing thermal settings UI with snapshot controls.
 * @param ui UI context.
 * @return None.
 */
void custom_init(lv_ui *ui)
{
    lv_obj_t *device_info_label;

    if (ui == NULL)
    {
        return;
    }

    g_snapshot_ui = ui;
    thermal_gui_set_child_label_text(ui->WidgetsDemo_btn_save, "Snapshot");
    lv_obj_add_event_cb(ui->WidgetsDemo_btn_save, thermal_gui_snapshot_save_event_cb, LV_EVENT_CLICKED, ui);

    g_snapshot_entry_button = thermal_gui_create_custom_button(ui->WidgetsDemo_tab_system,
                                                               "图库",
                                                               8,
                                                               146,
                                                               118,
                                                               CFG_THERMAL_GUI_ACCENT_COLOR);
    lv_obj_set_size(g_snapshot_entry_button, 118, 38);
    lv_obj_add_event_cb(g_snapshot_entry_button, thermal_gui_gallery_open_event_cb, LV_EVENT_CLICKED, ui);

    g_snapshot_status_label = lv_label_create(ui->WidgetsDemo_tab_system);
    lv_label_set_text(g_snapshot_status_label, "Ready");
    lv_obj_set_pos(g_snapshot_status_label, 8, 188);
    lv_obj_set_width(g_snapshot_status_label, 250);
    lv_obj_set_style_text_font(g_snapshot_status_label, &lv_font_montserratMedium_19, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(g_snapshot_status_label, lv_color_hex(0xffd07a), LV_PART_MAIN | LV_STATE_DEFAULT);

    g_auto_snapshot_toggle_button = thermal_gui_create_custom_button(ui->WidgetsDemo_tab_system,
                                                                     "AutoCap OFF",
                                                                     136,
                                                                     108,
                                                                     122,
                                                                     0x48b3ffU);
    lv_obj_add_event_cb(g_auto_snapshot_toggle_button, thermal_gui_auto_snapshot_toggle_event_cb, LV_EVENT_CLICKED, ui);

    g_auto_snapshot_period_minus_button = thermal_gui_create_custom_button(ui->WidgetsDemo_tab_system,
                                                                           "-",
                                                                           136,
                                                                           154,
                                                                           34,
                                                                           0x7bc6ffU);
    lv_obj_add_event_cb(g_auto_snapshot_period_minus_button, thermal_gui_auto_snapshot_period_minus_event_cb, LV_EVENT_CLICKED, ui);

    g_auto_snapshot_period_label = lv_label_create(ui->WidgetsDemo_tab_system);
    lv_label_set_text(g_auto_snapshot_period_label, "3s");
    lv_obj_set_pos(g_auto_snapshot_period_label, 184, 162);
    lv_obj_set_style_text_font(g_auto_snapshot_period_label, &lv_font_montserratMedium_19, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(g_auto_snapshot_period_label, lv_color_hex(0xe6ebef), LV_PART_MAIN | LV_STATE_DEFAULT);

    g_auto_snapshot_period_plus_button = thermal_gui_create_custom_button(ui->WidgetsDemo_tab_system,
                                                                          "+",
                                                                          224,
                                                                          154,
                                                                          34,
                                                                          CFG_THERMAL_GUI_ACCENT_COLOR);
    lv_obj_add_event_cb(g_auto_snapshot_period_plus_button, thermal_gui_auto_snapshot_period_plus_event_cb, LV_EVENT_CLICKED, ui);

    lv_obj_set_pos(ui->WidgetsDemo_btn_device_info, 8, 216);
    lv_obj_set_size(ui->WidgetsDemo_btn_device_info, 250, 76);
    lv_obj_clear_flag(ui->WidgetsDemo_btn_device_info, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_color(ui->WidgetsDemo_btn_device_info, lv_color_hex(0x3d4853), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui->WidgetsDemo_btn_device_info, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    device_info_label = lv_obj_get_child(ui->WidgetsDemo_btn_device_info, 0);
    if ((device_info_label != NULL) && (lv_obj_check_type(device_info_label, &lv_label_class) != false))
    {
        lv_label_set_text(device_info_label, "设备信息\nSTM32N647 + Tiny1C\n本地截图已启用");
    }

    g_snapshot_fullscreen_button = thermal_gui_create_custom_button(ui->WidgetsDemo_fullscreen,
                                                                    "拍照",
                                                                    18,
                                                                    18,
                                                                    70,
                                                                    CFG_THERMAL_GUI_ACCENT_COLOR);
    lv_obj_add_event_cb(g_snapshot_fullscreen_button, thermal_gui_snapshot_save_fullscreen_event_cb, LV_EVENT_CLICKED, ui);

    thermal_gui_auto_snapshot_refresh_controls();
    thermal_gui_snapshot_set_status(app_filex_is_ready() ? "Ready" : "Init...");
}

/**
 * @brief Process one AutoCap cycle inside the GUI thread.
 * @param abnormal_detected Non-zero when an abnormal-hotspot box is currently present.
 * @param now_ms Current system tick in milliseconds.
 * @return None.
 */
void thermal_gui_auto_snapshot_process(uint8_t abnormal_detected, uint32_t now_ms)
{
    CHAR saved_name[CFG_APP_FILEX_SNAPSHOT_NAME_LEN];
    UINT status;
    char status_text[48];
    uint32_t period_ms;

    if ((g_auto_snapshot_enable == 0U) ||
        (abnormal_detected == 0U) ||
        (g_snapshot_ui == NULL) ||
        ((g_snapshot_gallery_screen != NULL) && (lv_scr_act() == g_snapshot_gallery_screen)))
    {
        return;
    }

    period_ms = (uint32_t)g_auto_snapshot_period_seconds * 1000U;
    if ((g_auto_snapshot_last_tick_ms != 0U) && ((now_ms - g_auto_snapshot_last_tick_ms) < period_ms))
    {
        return;
    }

    g_auto_snapshot_last_tick_ms = now_ms;
    saved_name[0] = '\0';
    status = thermal_gui_snapshot_capture_preview(g_snapshot_ui, saved_name, sizeof(saved_name));
    if (status == FX_SUCCESS)
    {
        (void)snprintf(status_text, sizeof(status_text), "Auto %s", saved_name);
    }
    else
    {
        (void)snprintf(status_text, sizeof(status_text), "Auto fail: %s", app_filex_status_to_string(status));
    }

    thermal_gui_snapshot_set_status(status_text);
    if (g_snapshot_gallery_status != NULL)
    {
        lv_label_set_text(g_snapshot_gallery_status, status_text);
    }
}
