/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#include "lvgl.h"
#include "widgets_init.h"

/**
 * @brief Handle the shared keyboard widget close events.
 * @param e LVGL event descriptor.
 * @return None.
 */
void kb_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *kb = lv_event_get_target(e);

    if ((code == LV_EVENT_READY) || (code == LV_EVENT_CANCEL))
    {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Bind a text area to the shared keyboard when focused.
 * @param e LVGL event descriptor.
 * @return None.
 */
void ta_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *ta = lv_event_get_target(e);
    lv_obj_t *kb = lv_event_get_user_data(e);

    if ((code == LV_EVENT_FOCUSED) || (code == LV_EVENT_CLICKED))
    {
#if LV_USE_KEYBOARD != 0
        lv_keyboard_set_textarea(kb, ta);
#endif
        lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
    else if ((code == LV_EVENT_CANCEL) || (code == LV_EVENT_DEFOCUSED))
    {
#if LV_USE_KEYBOARD != 0
        lv_keyboard_set_textarea(kb, NULL);
#endif
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
}
