#ifndef PC_MONITOR_LVGL_FONT_COMPAT_H
#define PC_MONITOR_LVGL_FONT_COMPAT_H

#include "lvgl.h"

/*
 LVGL font compatibility layer.
 The project enables Montserrat fonts according
 to lv_conf.h. Use available fonts only.
*/

#define PCMON_SMALL_FONT (&lv_font_montserrat_16)
#define PCMON_NORMAL_FONT (&lv_font_montserrat_16)

#endif
