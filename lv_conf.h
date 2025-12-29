/**
 * @file lv_conf.h
 * @brief LVGL configuration for simulator
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

//====================
// COLOR SETTINGS
//====================

// Color depth: 1 (1 byte per pixel), 8 (RGB332), 16 (RGB565), 32 (ARGB8888)
#define LV_COLOR_DEPTH 16

//==================
// MEMORY SETTINGS
//==================

// Size of the memory available for `lv_malloc()` in bytes (>= 2kB)
#define LV_MEM_SIZE (64U * 1024U)  // 64 KB

// Size of the memory expand for `lv_malloc()` in bytes
#define LV_MEM_EXPAND_SIZE 0

// Set an address for the memory pool instead of allocating it as a normal array
#define LV_MEM_ADR 0

// Use the standard `memcpy` and `memset` instead of LVGL's own functions
#define LV_MEMCPY_MEMSET_STD 1

//====================
// HAL SETTINGS
//====================

// Default display refresh period in milliseconds
#define LV_DEF_REFR_PERIOD 30

// Input device read period in milliseconds
#define LV_INDEV_DEF_READ_PERIOD 30

//========================
// OPERATING SYSTEM
//========================

// Select an operating system to use. Possible options:
// - LV_OS_NONE
// - LV_OS_PTHREAD
// - LV_OS_FREERTOS
// - LV_OS_CMSIS_RTOS2
// - LV_OS_RTTHREAD
// - LV_OS_WINDOWS
// - LV_OS_CUSTOM
#define LV_USE_PTHREAD 1

//====================
// FONT USAGE
//====================

// Montserrat fonts with various character sets and sizes
#define LV_FONT_MONTSERRAT_8  0
#define LV_FONT_MONTSERRAT_10 0
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_MONTSERRAT_26 0
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_30 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_MONTSERRAT_34 0
#define LV_FONT_MONTSERRAT_36 0
#define LV_FONT_MONTSERRAT_38 0
#define LV_FONT_MONTSERRAT_40 0
#define LV_FONT_MONTSERRAT_42 0
#define LV_FONT_MONTSERRAT_44 0
#define LV_FONT_MONTSERRAT_46 0
#define LV_FONT_MONTSERRAT_48 0

//===================
// WIDGET USAGE
//===================

/*--------------
 * Widgets
 *-------------*/

#define LV_USE_ANIMIMG    1
#define LV_USE_BAR        1
#define LV_USE_BUTTON     1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_CALENDAR   1
#define LV_USE_CANVAS     1
#define LV_USE_CHART      1
#define LV_USE_CHECKBOX   1
#define LV_USE_DROPDOWN   1
#define LV_USE_IMAGE      1
#define LV_USE_IMAGEBUTTON 1
#define LV_USE_KEYBOARD   1
#define LV_USE_LABEL      1
#define LV_USE_LED        1
#define LV_USE_LINE       1
#define LV_USE_LIST       1
#define LV_USE_MENU       1
#define LV_USE_MSGBOX     1
#define LV_USE_ROLLER     1
#define LV_USE_SCALE      1
#define LV_USE_SLIDER     1
#define LV_USE_SPAN       1
#define LV_USE_SPINBOX    1
#define LV_USE_SPINNER    1
#define LV_USE_SWITCH     1
#define LV_USE_TEXTAREA   1
#define LV_USE_TABLE      1
#define LV_USE_TABVIEW    1
#define LV_USE_TILEVIEW   1
#define LV_USE_WIN        1
#define LV_USE_WINDOW     1

#endif // LV_CONF_H
