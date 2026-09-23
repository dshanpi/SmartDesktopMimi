#include "ui_fonts.h"
#include "lvgl/src/libs/tiny_ttf/lv_tiny_ttf.h"
#include <stdio.h>

LV_FONT_DECLARE(lv_font_source_han_sans_sc_16_cjk)
LV_FONT_DECLARE(font_inter_semibold_32)
LV_FONT_DECLARE(font_inter_medium_24)
LV_FONT_DECLARE(font_inter_medium_22)
LV_FONT_DECLARE(font_inter_regular_18)
LV_FONT_DECLARE(font_inter_regular_16)
LV_FONT_DECLARE(font_inter_regular_36)

static lv_font_t *font_h1_cjk = NULL;
static lv_font_t *font_h2_cjk = NULL;
static lv_font_t *font_h3_cjk = NULL;
static lv_font_t *font_body_lg_cjk = NULL;
static lv_font_t *font_body_md_cjk = NULL;
static lv_font_t *font_clock_meta_cjk = NULL;
static lv_font_t font_h1_builtin_fallback;
static lv_font_t font_h2_builtin_fallback;
static lv_font_t font_h3_builtin_fallback;
static lv_font_t font_body_lg_builtin_fallback;
static lv_font_t font_body_md_builtin_fallback;
static lv_font_t font_clock_meta_builtin_fallback;
static bool fonts_ready = false;

static const char *const cjk_regular_font_paths[] = {
    "A:/usr/share/fonts/SarasaUiSC-Regular.ttf",
    "A:/mnt/UDISK/SarasaUiSC-Regular.ttf",
    "A:/mnt/UDISK/fonts/SarasaUiSC-Regular.ttf",
};

static const char *const cjk_title_font_paths[] = {
    "A:/usr/share/fonts/SarasaUiSC-SemiBold.ttf",
    "A:/mnt/UDISK/SarasaUiSC-SemiBold.ttf",
    "A:/mnt/UDISK/fonts/SarasaUiSC-SemiBold.ttf",
};

static lv_font_t *create_ttf_font_from_paths(const char *const *paths, size_t path_count, int32_t size)
{
#if LV_USE_TINY_TTF && LV_TINY_TTF_FILE_SUPPORT
    for (size_t i = 0; i < path_count; i++) {
        lv_font_t *font = lv_tiny_ttf_create_file(paths[i], size);
        if (font) {
            printf("[UI Fonts] Loaded CJK font: %s (%dpx)\n", paths[i], (int)size);
            return font;
        }
    }
#else
    LV_UNUSED(paths);
    LV_UNUSED(path_count);
    LV_UNUSED(size);
#endif
    return NULL;
}

static lv_font_t *create_builtin_fallback_font(lv_font_t *dst, const lv_font_t *base)
{
    *dst = *base;
    dst->fallback = &lv_font_source_han_sans_sc_16_cjk;
    return dst;
}

void ui_fonts_init(void)
{
    if (fonts_ready) return;

    font_h1_cjk = create_ttf_font_from_paths(cjk_title_font_paths,
                                             sizeof(cjk_title_font_paths) / sizeof(cjk_title_font_paths[0]),
                                             32);
    font_h2_cjk = create_ttf_font_from_paths(cjk_title_font_paths,
                                             sizeof(cjk_title_font_paths) / sizeof(cjk_title_font_paths[0]),
                                             24);
    font_h3_cjk = create_ttf_font_from_paths(cjk_title_font_paths,
                                             sizeof(cjk_title_font_paths) / sizeof(cjk_title_font_paths[0]),
                                             22);
    font_body_lg_cjk = create_ttf_font_from_paths(cjk_regular_font_paths,
                                                  sizeof(cjk_regular_font_paths) / sizeof(cjk_regular_font_paths[0]),
                                                  20);
    font_body_md_cjk = create_ttf_font_from_paths(cjk_regular_font_paths,
                                                  sizeof(cjk_regular_font_paths) / sizeof(cjk_regular_font_paths[0]),
                                                  18);
    font_clock_meta_cjk = create_ttf_font_from_paths(cjk_regular_font_paths,
                                                     sizeof(cjk_regular_font_paths) / sizeof(cjk_regular_font_paths[0]),
                                                     36);

    if (!font_h1_cjk)
        font_h1_cjk = create_builtin_fallback_font(&font_h1_builtin_fallback, &font_inter_semibold_32);
    if (!font_h2_cjk)
        font_h2_cjk = create_builtin_fallback_font(&font_h2_builtin_fallback, &font_inter_medium_24);
    if (!font_h3_cjk)
        font_h3_cjk = create_builtin_fallback_font(&font_h3_builtin_fallback, &font_inter_medium_22);
    if (!font_body_lg_cjk)
        font_body_lg_cjk = create_builtin_fallback_font(&font_body_lg_builtin_fallback, &font_inter_regular_18);
    if (!font_body_md_cjk)
        font_body_md_cjk = create_builtin_fallback_font(&font_body_md_builtin_fallback, &font_inter_regular_16);
    if (!font_clock_meta_cjk)
        font_clock_meta_cjk = create_builtin_fallback_font(&font_clock_meta_builtin_fallback, &font_inter_regular_36);

    fonts_ready = true;
}

const lv_font_t *ui_font_h1(void)
{
    ui_fonts_init();
    return font_h1_cjk;
}

const lv_font_t *ui_font_h2(void)
{
    ui_fonts_init();
    return font_h2_cjk;
}

const lv_font_t *ui_font_h3(void)
{
    ui_fonts_init();
    return font_h3_cjk;
}

const lv_font_t *ui_font_body_lg(void)
{
    ui_fonts_init();
    return font_body_lg_cjk;
}

const lv_font_t *ui_font_body_md(void)
{
    ui_fonts_init();
    return font_body_md_cjk;
}

const lv_font_t *ui_font_clock_meta(void)
{
    ui_fonts_init();
    return font_clock_meta_cjk;
}
