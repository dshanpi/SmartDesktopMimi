/**
 * @file number_flow_c.cpp
 * @brief NumberFlow C 接口实现
 */
#include "number_flow_c.h"
#include "number_flow.hpp"
#include "core/anim_registry.hpp"

static void number_flow_update_cb(void *handle)
{
    if (handle) {
        ((lv_ui::NumberFlow *)handle)->update();
    }
}

static void number_flow_destroy_cb(void *handle)
{
    if (handle) {
        delete (lv_ui::NumberFlow *)handle;
    }
}

void* number_flow_create(void* parent) {
    auto* nf = new lv_ui::NumberFlow((lv_obj_t*)parent);
    lv_ui::anim::register_anim(nf, number_flow_update_cb, number_flow_destroy_cb);
    return nf;
}

void number_flow_init(void* handle) {
    if (handle) {
        ((lv_ui::NumberFlow*)handle)->init();
    }
}

void number_flow_set_font(void* handle, const lv_font_t* font) {
    if (handle && font) {
        ((lv_ui::NumberFlow*)handle)->setTextFont(font);
    }
}

void number_flow_set_color(void* handle, lv_color_t color) {
    if (handle) {
        ((lv_ui::NumberFlow*)handle)->setTextColor(color);
    }
}

void number_flow_set_prefix(void* handle, const char* prefix) {
    if (handle && prefix) {
        ((lv_ui::NumberFlow*)handle)->setPrefix(prefix);
    }
}

void number_flow_set_suffix(void* handle, const char* suffix) {
    if (handle && suffix) {
        ((lv_ui::NumberFlow*)handle)->setSuffix(suffix);
    }
}

void number_flow_set_value(void* handle, int value) {
    if (handle) {
        ((lv_ui::NumberFlow*)handle)->setValue(value);
    }
}

void number_flow_set_value_immediate(void* handle, int value) {
    if (handle) {
        ((lv_ui::NumberFlow*)handle)->setValueImmediate(value);
    }
}

void number_flow_set_clock_preset(void* handle)
{
    if (handle) {
        /* Hour/minute: slightly slower, cubic ease — calmer on the main clock. */
        ((lv_ui::NumberFlow*)handle)->setDigitEasingTuning(
            0.22f, lv_ui::easing::ease_out_cubic);
    }
}

void number_flow_set_second_preset(void* handle)
{
    if (handle) {
        /* Seconds: shorter + quad ease — snappier 1 Hz ticks, less CPU time animating. */
        ((lv_ui::NumberFlow*)handle)->setDigitEasingTuning(
            0.14f, lv_ui::easing::ease_out_quad);
    }
}

void number_flow_set_min_digits(void* handle, int min_digits)
{
    if (handle) {
        ((lv_ui::NumberFlow*)handle)->setMinDigits(min_digits);
    }
}

void number_flow_update(void* handle) {
    number_flow_update_cb(handle);
}

void number_flow_delete(void* handle) {
    if (handle) {
        lv_ui::anim::unregister_anim(handle);
        number_flow_destroy_cb(handle);
    }
}

void number_flow_update_all(void) {
    lv_ui::anim::update_all();
}
