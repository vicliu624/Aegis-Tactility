// SPDX-License-Identifier: Apache-2.0
#include <lvgl.h>
#include <tactility/lvgl_fonts.h>
#include <tactility/check.h>

// The preprocessor definitions that are used below are defined in the CMakeLists.txt from this module.

extern const lv_font_t TT_LVGL_TEXT_FONT_SMALL_SYMBOL;
extern const lv_font_t TT_LVGL_TEXT_FONT_DEFAULT_SYMBOL;
extern const lv_font_t TT_LVGL_TEXT_FONT_LARGE_SYMBOL;

extern const lv_font_t TT_LVGL_LAUNCHER_FONT_ICON_SYMBOL;
extern const lv_font_t TT_LVGL_STATUSBAR_FONT_ICON_SYMBOL;
extern const lv_font_t TT_LVGL_SHARED_FONT_ICON_SYMBOL;

static lv_font_t text_font_small;
static lv_font_t text_font_default;
static lv_font_t text_font_large;
static bool text_fonts_initialized;

static void init_text_fonts(void) {
    if (text_fonts_initialized) return;
    text_font_small = TT_LVGL_TEXT_FONT_SMALL_SYMBOL;
    text_font_default = TT_LVGL_TEXT_FONT_DEFAULT_SYMBOL;
    text_font_large = TT_LVGL_TEXT_FONT_LARGE_SYMBOL;
    text_fonts_initialized = true;
}

static lv_font_t* get_mutable_text_font(enum LvglFontSize font_size) {
    init_text_fonts();
    switch (font_size) {
        case FONT_SIZE_SMALL: return &text_font_small;
        case FONT_SIZE_DEFAULT: return &text_font_default;
        case FONT_SIZE_LARGE: return &text_font_large;
        default: check(false);
    }
}

uint32_t lvgl_get_text_font_height(enum LvglFontSize font_size) {
    switch (font_size) {
        case FONT_SIZE_SMALL: return TT_LVGL_TEXT_FONT_SMALL_SIZE;
        case FONT_SIZE_DEFAULT: return TT_LVGL_TEXT_FONT_DEFAULT_SIZE;
        case FONT_SIZE_LARGE: return TT_LVGL_TEXT_FONT_LARGE_SIZE;
        default: check(false);
    }
}
const lv_font_t* lvgl_get_text_font(enum LvglFontSize font_size) {
    return get_mutable_text_font(font_size);
}

void lvgl_set_text_font_fallback(enum LvglFontSize font_size, const lv_font_t* fallback_font) {
    get_mutable_text_font(font_size)->fallback = fallback_font;
}

void lvgl_reset_text_font_fallbacks(void) {
    init_text_fonts();
    text_font_small.fallback = NULL;
    text_font_default.fallback = NULL;
    text_font_large.fallback = NULL;
}

uint32_t lvgl_get_shared_icon_font_height() { return TT_LVGL_SHARED_FONT_ICON_SIZE; }

const lv_font_t* lvgl_get_shared_icon_font() { return &TT_LVGL_SHARED_FONT_ICON_SYMBOL; }

uint32_t lvgl_get_launcher_icon_font_height() { return TT_LVGL_LAUNCHER_FONT_ICON_SIZE; }

const lv_font_t* lvgl_get_launcher_icon_font() { return &TT_LVGL_LAUNCHER_FONT_ICON_SYMBOL; }

uint32_t lvgl_get_statusbar_icon_font_height() { return TT_LVGL_STATUSBAR_FONT_ICON_SIZE; }

const lv_font_t* lvgl_get_statusbar_icon_font() { return &TT_LVGL_STATUSBAR_FONT_ICON_SYMBOL; }
