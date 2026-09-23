/**
 * @file number_flow.hpp
 * @brief 数字滚动动画控件
 * Refs: https://number-flow.barvian.me/
 */
#pragma once
#include "animate_value.hpp"
#include <lvgl.h>
#include <vector>
#include <string>
#include <string_view>

namespace lv_ui {

class DigitFlow {
public:
    DigitFlow() : _obj(nullptr), _font_height(0), _font_width(0), _font(nullptr), _current_index(1) {
        for (int i = 0; i < 12; i++) _labels[i] = nullptr;
    }
    DigitFlow(lv_obj_t* parent);
    ~DigitFlow();

    // 禁用拷贝
    DigitFlow(const DigitFlow&) = delete;
    DigitFlow& operator=(const DigitFlow&) = delete;

    // 启用移动
    DigitFlow(DigitFlow&& other) noexcept;
    DigitFlow& operator=(DigitFlow&& other) noexcept;

    void setTextFont(const lv_font_t* font, lv_style_selector_t selector = LV_PART_MAIN);
    void setTextColor(lv_color_t color);

    void increaseTo(int target);
    void decreaseTo(int target);
    /** Jump to digit with no reel animation (time sync / first paint). */
    void teleportTo(int target);

    void update();
    int value() const { return _digit_list[_current_index]; }

    AnimateValue& flowAnimate() { return _y_offset; }
    void setPos(int x, int y);
    void setOpa(uint8_t opa);

private:
    // 12个数字循环: 9,0,1,2,3,4,5,6,7,8,9,0
    static constexpr int _digit_list[12] = {9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0};

    lv_obj_t* _obj = nullptr;
    lv_obj_t* _labels[12];  // 12个Label，使用原始指针数组
    AnimateValue _y_offset;
    int _current_index = 1;  // 从 0 开始

    int32_t _font_height = 0;
    int32_t _font_width = 0;
    const lv_font_t* _font = nullptr;

    void increase();
    void decrease();
    void init();
};


class NumberFlow {
public:
    NumberFlow(lv_obj_t* parent);
    ~NumberFlow();

    // 配置
    void setTextFont(const lv_font_t* font);
    void setTextColor(lv_color_t color);
    void setPrefix(std::string_view prefix);
    void setSuffix(std::string_view suffix);
    void setPrefixColor(lv_color_t color);
    void setSuffixColor(lv_color_t color);
    void setShowPositiveSign(bool show);
    void setMinDigits(int min_digits);
    void setDigitSpringTuning(float stiffness, float damping, float visual_duration,
                              float rest_speed = 0.5f, float rest_delta = 0.5f);
    /* duration in seconds; ease defaults to ease_out_quad when null. */
    void setDigitEasingTuning(float duration, float (*ease)(float) = nullptr);

    // 动画类型
    AnimationType animationType = AnimationType::Spring;

    // 初始化
    void init();

    // 每帧调用
    void update();

    // 赋值（默认滚动动画）
    void setValue(int value);
    /** Assign without rolling — for large wall-clock jumps after NTP/boot. */
    void setValueImmediate(int value);
    int value() const { return _current_value; }

    // 获取总宽度
    int getTotalWidth();

private:
    lv_obj_t* _obj = nullptr;
    const lv_font_t* _font = nullptr;
    lv_color_t _text_color = lv_color_white();

    int _current_value = 0;
    int _last_value = 0;
    int _current_digits = 1;

    std::vector<DigitFlow> _digits;
    lv_obj_t* _label_sign = nullptr;
    lv_obj_t* _label_prefix = nullptr;
    lv_obj_t* _label_suffix = nullptr;

    std::string _prefix;
    std::string _suffix;
    bool _show_positive_sign = false;

    AnimateValue _sign_x{0}, _sign_opa{0};
    AnimateValue _prefix_x{0}, _prefix_opa{0};
    AnimateValue _suffix_x{0}, _suffix_opa{0};

    int _cached_prefix_width = 0;
    int _cached_suffix_width = 0;

    int _font_width = 0;
    int _font_height = 0;
    int _min_digits = 1;
    float _digit_spring_stiffness = 180.0f;
    float _digit_spring_damping = 12.0f;
    float _digit_spring_visual_duration = 0.3f;
    float _digit_spring_rest_speed = 0.5f;
    float _digit_spring_rest_delta = 0.5f;
    float _digit_easing_duration = 0.18f;
    float (*_digit_easing_fn)(float) = nullptr;

    int get_actual_digits(int num);
    int get_sign_width();
    int get_text_width(const char* text);
    void create_digits(int count);

    void handle_digit_number_changed(bool animate);
    void handle_sign_changed();
    void handle_prefix_changed();
    void handle_suffix_changed();
    void update_layout();
};

} // namespace lv_ui
