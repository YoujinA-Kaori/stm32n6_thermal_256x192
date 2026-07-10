// SPDX-License-Identifier: MIT
// Copyright 2020 NXP

#ifndef __CUSTOM_H_
#define __CUSTOM_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "gui_guider.h"

void custom_init(lv_ui *ui);
void thermal_gui_auto_snapshot_process(uint8_t abnormal_detected, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
#endif
