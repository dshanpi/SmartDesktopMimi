/**
 * @file number_flow.cpp
 * @brief 数字滚动动画控件实现
 */
#include "number_flow.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace lv_ui {

// ==================== DigitFlow ====================

DigitFlow::DigitFlow(lv_obj_t* parent) {
    _obj = lv_obj_create(parent);
    lv_obj_set_size(_obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(_obj, 0, 0);
    lv_obj_set_style_outline_width(_obj, 0, 0);
    lv_obj_set_style_radius(_obj, 0, 0);
    lv_obj_set_style_border_width(_obj, 0, 0);
    lv_obj_clear_flag(_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(_obj, LV_OPA_TRANSP, 0);

    // 初始化12个Label
    for (int i = 0; i < 12; i++) {
        _labels[i] = lv_label_create(_obj);
        lv_label_set_text(_labels[i], std::to_string(_digit_list[i]).c_str());
        lv_obj_set_align(_labels[i], LV_ALIGN_TOP_LEFT);
    }
}

DigitFlow::~DigitFlow() {
    if (_obj) {
        lv_obj_del(_obj);
        _obj = nullptr;
    }
}

DigitFlow::DigitFlow(DigitFlow&& other) noexcept
    : _obj(other._obj)
    , _y_offset(std::move(other._y_offset))
    , _current_index(other._current_index)
    , _font_height(other._font_height)
    , _font(other._font)
{
    for (int i = 0; i < 12; i++) {
        _labels[i] = other._labels[i];
        other._labels[i] = nullptr;
    }
    other._obj = nullptr;
}

DigitFlow& DigitFlow::operator=(DigitFlow&& other) noexcept {
    if (this != &other) {
        if (_obj) lv_obj_del(_obj);
        _obj = other._obj;
        other._obj = nullptr;
        _y_offset = std::move(other._y_offset);
        _current_index = other._current_index;
        _font_height = other._font_height;
        _font = other._font;
        for (int i = 0; i < 12; i++) {
            _labels[i] = other._labels[i];
            other._labels[i] = nullptr;
        }
    }
    return *this;
}

void DigitFlow::init() {
    _font_height = lv_font_get_line_height(_font);
    _font_width = lv_font_get_glyph_width(_font, '0', '0');
    if (_font_width <= 0) {
        _font_width = LV_MAX(8, _font_height / 2);
    }
    _y_offset.teleport(_current_index * _font_height);
    lv_obj_set_size(_obj, _font_width, _font_height);

    for (int i = 0; i < 12; i++) {
        lv_obj_set_width(_labels[i], _font_width);
        lv_obj_set_style_text_align(_labels[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(_labels[i], 0, i * _font_height - (int)_y_offset.directValue());
    }
}

void DigitFlow::setTextFont(const lv_font_t* font, lv_style_selector_t selector) {
    _font = font;
    for (int i = 0; i < 12; i++) {
        lv_obj_set_style_text_font(_labels[i], font, selector);
    }
    init();
}

void DigitFlow::setTextColor(lv_color_t color) {
    for (int i = 0; i < 12; i++) {
        lv_obj_set_style_text_color(_labels[i], color, 0);
    }
}

void DigitFlow::increase() {
    /* Tape is [9,0,1,...,9,0]. Index 1 and 11 are both digit 0.
     * At the end-0 (11), snap to the mid-0 (1) with no visible jump, then
     * step forward — avoids animating backward across the whole reel. */
    if (_current_index >= 11) {
        _current_index = 1;
        _y_offset.teleport(static_cast<float>(_font_height));
    }
    if (_current_index < 11) {
        _current_index++;
    }
    _y_offset.setTarget(static_cast<float>(_current_index * _font_height));
}

void DigitFlow::decrease() {
    /* Symmetric wrap: mid-0 (1) snaps to end-0 (11), then steps backward. */
    if (_current_index <= 1) {
        _current_index = 11;
        _y_offset.teleport(static_cast<float>(11 * _font_height));
    }
    if (_current_index > 0) {
        _current_index--;
    }
    _y_offset.setTarget(static_cast<float>(_current_index * _font_height));
}

void DigitFlow::increaseTo(int target) {
    int target_digit = std::abs(target) % 10;
    if (value() == target_digit) return;

    /* One forward hop on the tape (may cross several digits). Remap end-0
     * first so the destination index is always greater than the start. */
    if (_current_index >= 11) {
        _current_index = 1;
        _y_offset.teleport(static_cast<float>(_font_height));
    }

    int dest = _current_index + 1;
    while (dest < 12 && _digit_list[dest] != target_digit) {
        dest++;
    }
    if (dest >= 12) {
        /* Should be rare; fall back to preferred index for the digit. */
        dest = (target_digit == 0) ? 1 : (target_digit + 1);
        _y_offset.teleport(static_cast<float>(dest * _font_height));
        _current_index = dest;
        return;
    }

    _current_index = dest;
    _y_offset.setTarget(static_cast<float>(dest * _font_height));
}

void DigitFlow::decreaseTo(int target) {
    int target_digit = std::abs(target) % 10;
    if (value() == target_digit) return;

    if (_current_index <= 1) {
        _current_index = 11;
        _y_offset.teleport(static_cast<float>(11 * _font_height));
    }

    int dest = _current_index - 1;
    while (dest >= 0 && _digit_list[dest] != target_digit) {
        dest--;
    }
    if (dest < 0) {
        dest = (target_digit == 0) ? 11 : (target_digit + 1);
        _y_offset.teleport(static_cast<float>(dest * _font_height));
        _current_index = dest;
        return;
    }

    _current_index = dest;
    _y_offset.setTarget(static_cast<float>(dest * _font_height));
}

void DigitFlow::teleportTo(int target) {
    int target_digit = std::abs(target) % 10;
    /* Prefer the mid-tape copy of each digit (index 1 = 0, 2 = 1, … 10 = 9). */
    int dest = (target_digit == 0) ? 1 : (target_digit + 1);
    _current_index = dest;
    _y_offset.teleport(static_cast<float>(dest * _font_height));
    const int y_off = dest * _font_height;
    for (int i = 0; i < 12; i++) {
        if (_labels[i]) {
            lv_obj_set_pos(_labels[i], 0, i * _font_height - y_off);
        }
    }
}

void DigitFlow::update() {
    const bool was_done = _y_offset.done();
    const int prev_off = static_cast<int>(_y_offset.directValue());
    _y_offset.update();
    const int y_off = static_cast<int>(_y_offset.value());

    /* Soft path without G2D: skip 12 label moves while the reel is idle. */
    if (was_done && _y_offset.done() && prev_off == y_off) {
        return;
    }

    for (int i = 0; i < 12; i++) {
        lv_obj_set_pos(_labels[i], 0, i * _font_height - y_off);
    }
}

void DigitFlow::setPos(int x, int y) {
    lv_obj_set_pos(_obj, x, y);
}

void DigitFlow::setOpa(uint8_t opa) {
    lv_obj_set_style_opa(_obj, opa, 0);
}


// ==================== NumberFlow ====================

NumberFlow::NumberFlow(lv_obj_t* parent) {
    _obj = lv_obj_create(parent);
    lv_obj_set_size(_obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(_obj, 0, 0);
    lv_obj_clear_flag(_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_obj, 0, 0);
    lv_obj_set_style_outline_width(_obj, 0, 0);
}

NumberFlow::~NumberFlow() {
    if (_obj) {
        lv_obj_del(_obj);
        _obj = nullptr;
    }
}

void NumberFlow::init() {
    _font_height = lv_font_get_line_height(_font);
    _font_width = lv_font_get_glyph_width(_font, '0', '0');
    if (_font_width <= 0) {
        _font_width = LV_MAX(8, _font_height / 2);
    }

    // 设置初始值
    setValue(_current_value);
}

void NumberFlow::setTextFont(const lv_font_t* font) {
    _font = font;
    for (auto& digit : _digits) {
        digit.setTextFont(font);
    }
}

void NumberFlow::setTextColor(lv_color_t color) {
    _text_color = color;
    for (auto& digit : _digits) {
        digit.setTextColor(color);
    }
}

void NumberFlow::setPrefix(std::string_view prefix) {
    _prefix = prefix;
    handle_prefix_changed();
}

void NumberFlow::setSuffix(std::string_view suffix) {
    _suffix = suffix;
    handle_suffix_changed();
}

void NumberFlow::setPrefixColor(lv_color_t color) {
    if (_label_prefix) {
        lv_obj_set_style_text_color(_label_prefix, color, 0);
    }
}

void NumberFlow::setSuffixColor(lv_color_t color) {
    if (_label_suffix) {
        lv_obj_set_style_text_color(_label_suffix, color, 0);
    }
}

void NumberFlow::setShowPositiveSign(bool show) {
    _show_positive_sign = show;
    handle_sign_changed();
}

void NumberFlow::setMinDigits(int min_digits) {
    _min_digits = LV_MAX(1, min_digits);
    setValue(_current_value);
}

void NumberFlow::setDigitSpringTuning(float stiffness, float damping, float visual_duration,
                                      float rest_speed, float rest_delta) {
    animationType = AnimationType::Spring;
    _digit_spring_stiffness = stiffness;
    _digit_spring_damping = damping;
    _digit_spring_visual_duration = visual_duration;
    _digit_spring_rest_speed = rest_speed;
    _digit_spring_rest_delta = rest_delta;

    for (auto& digit : _digits) {
        digit.flowAnimate().type = AnimationType::Spring;
        digit.flowAnimate().spring().stiffness = _digit_spring_stiffness;
        digit.flowAnimate().spring().damping = _digit_spring_damping;
        digit.flowAnimate().spring().visualDuration = _digit_spring_visual_duration;
        digit.flowAnimate().spring().restSpeed = _digit_spring_rest_speed;
        digit.flowAnimate().spring().restDelta = _digit_spring_rest_delta;
    }
}

void NumberFlow::setDigitEasingTuning(float duration, float (*ease)(float)) {
    animationType = AnimationType::Easing;
    _digit_easing_duration = duration;
    _digit_easing_fn = ease ? ease : easing::ease_out_quad;

    for (auto& digit : _digits) {
        digit.flowAnimate().type = AnimationType::Easing;
        digit.flowAnimate().easing().duration = _digit_easing_duration;
        digit.flowAnimate().easing().easing = _digit_easing_fn;
    }
}

void NumberFlow::update() {
    for (auto& digit : _digits) {
        digit.update();
    }

    _sign_x.update();
    _sign_opa.update();
    _prefix_x.update();
    _prefix_opa.update();
    _suffix_x.update();
    _suffix_opa.update();

    update_layout();
}

void NumberFlow::setValue(int value) {
    if (_font == nullptr) return;

    _last_value = _current_value;
    _current_value = value;

    if (_digits.empty()) {
        _current_digits = get_actual_digits(value);
        create_digits(_current_digits);
    }

    handle_digit_number_changed(true);
    handle_sign_changed();
    handle_prefix_changed();
    handle_suffix_changed();
}

void NumberFlow::setValueImmediate(int value) {
    if (_font == nullptr) return;

    _last_value = value;
    _current_value = value;

    if (_digits.empty()) {
        _current_digits = get_actual_digits(value);
        create_digits(_current_digits);
    }

    handle_digit_number_changed(false);
    handle_sign_changed();
    handle_prefix_changed();
    handle_suffix_changed();
}

int NumberFlow::getTotalWidth() {
    if (_font == nullptr) return 0;

    int prefix_w = _label_prefix ? get_text_width(_prefix.c_str()) : 0;
    int sign_w = get_sign_width();
    int digits_w = _current_digits * _font_width;
    int suffix_w = _label_suffix ? get_text_width(_suffix.c_str()) : 0;

    return prefix_w + sign_w + digits_w + suffix_w;
}

int NumberFlow::get_actual_digits(int num) {
    if (num == 0) return _min_digits;
    int count = 0;
    int temp = std::abs(num);
    while (temp != 0) {
        temp /= 10;
        count++;
    }
    return LV_MAX(_min_digits, count);
}

int NumberFlow::get_sign_width() {
    if (!_label_sign || _sign_opa.directValue() < 1) return 0;
    return lv_font_get_glyph_width(_font, _current_value < 0 ? '-' : '+', '0');
}

int NumberFlow::get_text_width(const char* text) {
    int width = 0;
    while (*text) {
        width += lv_font_get_glyph_width(_font, *text, *text);
        text++;
    }
    return width;
}

void NumberFlow::create_digits(int count) {
    float (*ease_fn)(float) = _digit_easing_fn ? _digit_easing_fn : easing::ease_out_quad;
    _digits.resize(count);
    for (int i = 0; i < count; i++) {
        _digits[i] = DigitFlow(_obj);
        _digits[i].setTextFont(_font);
        _digits[i].setTextColor(_text_color);
        _digits[i].flowAnimate().type = animationType;
        _digits[i].flowAnimate().spring().stiffness = _digit_spring_stiffness;
        _digits[i].flowAnimate().spring().damping = _digit_spring_damping;
        _digits[i].flowAnimate().spring().visualDuration = _digit_spring_visual_duration;
        _digits[i].flowAnimate().spring().restSpeed = _digit_spring_rest_speed;
        _digits[i].flowAnimate().spring().restDelta = _digit_spring_rest_delta;
        _digits[i].flowAnimate().easing().duration = _digit_easing_duration;
        _digits[i].flowAnimate().easing().easing = ease_fn;
    }
}

void NumberFlow::handle_digit_number_changed(bool animate) {
    int new_digits = get_actual_digits(_current_value);

    float (*ease_fn)(float) = _digit_easing_fn ? _digit_easing_fn : easing::ease_out_quad;

    // 添加新 digit
    while (_current_digits < new_digits) {
        DigitFlow df(_obj);
        df.setTextFont(_font);
        df.setTextColor(_text_color);
        df.flowAnimate().type = animationType;
        df.flowAnimate().spring().stiffness = _digit_spring_stiffness;
        df.flowAnimate().spring().damping = _digit_spring_damping;
        df.flowAnimate().spring().visualDuration = _digit_spring_visual_duration;
        df.flowAnimate().spring().restSpeed = _digit_spring_rest_speed;
        df.flowAnimate().spring().restDelta = _digit_spring_rest_delta;
        df.flowAnimate().easing().duration = _digit_easing_duration;
        df.flowAnimate().easing().easing = ease_fn;
        _digits.push_back(std::move(df));
        _current_digits++;
    }

    // 移除多余 digit
    while (_current_digits > new_digits && _current_digits > 1) {
        _digits.pop_back();
        _current_digits--;
    }

    _current_digits = new_digits;

    // 获取每个数字
    int number = std::abs(_current_value);
    int divisor = (int)std::pow(10, _current_digits - 1);
    int actual = get_actual_digits(number);
    if (actual == 0) actual = 1;

    for (int i = 0; i < _current_digits; i++) {
        int digit;
        if (i < (_current_digits - actual)) {
            digit = 0;
        } else {
            digit = number / divisor;
            number %= divisor;
        }
        divisor /= 10;

        bool increase = (_last_value < _current_value);
        if (_current_value < 0) increase = !increase;

        if ((int)_digits[i].value() != digit) {
            if (!animate) {
                _digits[i].teleportTo(digit);
            } else if (increase) {
                _digits[i].increaseTo(digit);
            } else {
                _digits[i].decreaseTo(digit);
            }
        }
    }
}

void NumberFlow::handle_sign_changed() {
    bool need_sign = (_current_value < 0) || (_show_positive_sign && _current_value > 0);

    if (!need_sign) {
        if (_label_sign) {
            _sign_opa.setTarget(0);
            if (_sign_opa.done()) {
                lv_obj_del(_label_sign);
                _label_sign = nullptr;
            }
        }
        return;
    }

    if (!_label_sign) {
        _label_sign = lv_label_create(_obj);
        lv_label_set_text(_label_sign, _current_value < 0 ? "-" : "+");
        lv_obj_set_style_text_font(_label_sign, _font, 0);
        lv_obj_set_style_text_color(_label_sign, _text_color, 0);
        _sign_x.teleport(0);
        _sign_opa.teleport(0);
    }

    const char* sign_text = _current_value < 0 ? "-" : "+";
    if (std::strcmp(lv_label_get_text(_label_sign), sign_text) != 0) {
        lv_label_set_text(_label_sign, sign_text);
        _sign_opa.teleport(0);
    }

    _sign_opa.setTarget(255);
    _sign_x.setTarget(_cached_prefix_width);
}

void NumberFlow::handle_prefix_changed() {
    if (_prefix.empty()) {
        if (_label_prefix) {
            _prefix_opa.setTarget(0);
            if (_prefix_opa.done()) {
                lv_obj_del(_label_prefix);
                _label_prefix = nullptr;
            }
        }
        return;
    }

    if (!_label_prefix) {
        _label_prefix = lv_label_create(_obj);
        lv_label_set_text(_label_prefix, _prefix.c_str());
        lv_obj_set_style_text_font(_label_prefix, _font, 0);
        lv_obj_set_style_text_color(_label_prefix, _text_color, 0);
        _prefix_x.teleport(0);
        _prefix_opa.teleport(0);
    }

    if (std::strcmp(lv_label_get_text(_label_prefix), _prefix.c_str()) != 0) {
        lv_label_set_text(_label_prefix, _prefix.c_str());
        _prefix_opa.teleport(0);
    }

    _cached_prefix_width = get_text_width(_prefix.c_str());
    _prefix_opa.setTarget(255);
    _prefix_x.setTarget(0);
}

void NumberFlow::handle_suffix_changed() {
    if (_suffix.empty()) {
        if (_label_suffix) {
            _suffix_opa.setTarget(0);
            if (_suffix_opa.done()) {
                lv_obj_del(_label_suffix);
                _label_suffix = nullptr;
            }
        }
        return;
    }

    if (!_label_suffix) {
        _label_suffix = lv_label_create(_obj);
        lv_label_set_text(_label_suffix, _suffix.c_str());
        lv_obj_set_style_text_font(_label_suffix, _font, 0);
        lv_obj_set_style_text_color(_label_suffix, _text_color, 0);
        _suffix_opa.teleport(0);
    }

    if (std::strcmp(lv_label_get_text(_label_suffix), _suffix.c_str()) != 0) {
        lv_label_set_text(_label_suffix, _suffix.c_str());
        _suffix_opa.teleport(0);
    }

    _cached_suffix_width = get_text_width(_suffix.c_str());
    _suffix_opa.setTarget(255);
}

void NumberFlow::update_layout() {
    // 更新 prefix
    if (_label_prefix) {
        lv_obj_set_pos(_label_prefix, (int)_prefix_x.value(), 0);
        lv_obj_set_style_opa(_label_prefix, (uint8_t)_prefix_opa.value(), 0);
    }

    // 更新 sign
    if (_label_sign) {
        lv_obj_set_pos(_label_sign, _cached_prefix_width + (int)_sign_x.value(), 0);
        lv_obj_set_style_opa(_label_sign, (uint8_t)_sign_opa.value(), 0);
    }

    // 更新 digits
    int x_offset = _cached_prefix_width + get_sign_width();
    for (int i = 0; i < (int)_digits.size(); i++) {
        _digits[i].setPos(x_offset + i * _font_width, 0);
    }

    // 更新 suffix
    if (_label_suffix) {
        int suffix_x = x_offset + _current_digits * _font_width;
        lv_obj_set_pos(_label_suffix, suffix_x, 0);
        lv_obj_set_style_opa(_label_suffix, (uint8_t)_suffix_opa.value(), 0);
    }

    /* Ensure parent object has explicit size for flex layout measurement. */
    lv_obj_set_size(_obj, LV_MAX(1, getTotalWidth()), LV_MAX(1, _font_height));
}

} // namespace lv_ui
