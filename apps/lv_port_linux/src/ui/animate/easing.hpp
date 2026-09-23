/**
 * @file easing.hpp
 * @brief 缓动函数集合
 * Refs: https://easings.net
 */
#pragma once
#include <cmath>

namespace lv_ui {

namespace easing {

inline float linear(float t) {
    return t;
}

inline float ease_in_sine(float t) {
    return 1 - std::cos((t * M_PI) / 2);
}

inline float ease_out_sine(float t) {
    return std::sin((t * M_PI) / 2);
}

inline float ease_in_out_sine(float t) {
    return -(std::cos(M_PI * t) - 1) / 2;
}

inline float ease_in_quad(float t) {
    return t * t;
}

inline float ease_out_quad(float t) {
    return 1 - (1 - t) * (1 - t);
}

inline float ease_in_out_quad(float t) {
    return t < 0.5f ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2) / 2;
}

inline float ease_in_cubic(float t) {
    return t * t * t;
}

inline float ease_out_cubic(float t) {
    return 1 - std::pow(1 - t, 3);
}

inline float ease_in_out_cubic(float t) {
    return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
}

inline float ease_in_quart(float t) {
    return t * t * t * t;
}

inline float ease_out_quart(float t) {
    return 1 - std::pow(1 - t, 4);
}

inline float ease_in_out_quart(float t) {
    return t < 0.5f ? 8 * t * t * t * t : 1 - std::pow(-2 * t + 2, 4) / 2;
}

inline float ease_out_expo(float t) {
    return t == 1.0f ? 1.0f : 1 - std::pow(2, -10 * t);
}

inline float ease_in_out_expo(float t) {
    if (t == 0.0f) return 0.0f;
    if (t == 1.0f) return 1.0f;
    if (t < 0.5f) return std::pow(2, 20 * t - 10) / 2;
    return (2 - std::pow(2, -20 * t + 10)) / 2;
}

inline float ease_out_bounce(float t) {
    constexpr float n1 = 7.5625f;
    constexpr float d1 = 2.75f;

    if (t < 1 / d1) {
        return n1 * t * t;
    } else if (t < 2 / d1) {
        t -= 1.5f / d1;
        return n1 * t * t + 0.75f;
    } else if (t < 2.5f / d1) {
        t -= 2.25f / d1;
        return n1 * t * t + 0.9375f;
    } else {
        t -= 2.625f / d1;
        return n1 * t * t + 0.984375f;
    }
}

} // namespace easing

} // namespace lv_ui
