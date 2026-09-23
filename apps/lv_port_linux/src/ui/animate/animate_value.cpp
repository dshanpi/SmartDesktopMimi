/**
 * @file animate_value.cpp
 * @brief 动画值实现
 */
#include "animate_value.hpp"
#include <cmath>

namespace lv_ui {

void AnimateValue::setTarget(float target) {
    if (_done) {
        _spring_velocity = 0.0f;
    }
    _last_tick = hal_get_tick();
    _easing_start = _current;
    _easing_elapsed = 0.0f;
    _target = target;
    _done = false;
}

void AnimateValue::teleport(float value) {
    _current = value;
    _target = value;
    _value = value;
    _done = true;
    _spring_velocity = 0.0f;
    _easing_start = value;
    _easing_elapsed = 0.0f;
}

void AnimateValue::update() {
    if (_done) {
        _value = _current;
        return;
    }

    uint32_t now = hal_get_tick();
    uint32_t dt_ms = now - _last_tick;
    _last_tick = now;

    /* Clamp shared path so both spring and easing survive boot jank / long stalls.
     * Uncapped easing with ~0.14s duration collapses to a single frame under load,
     * which looks like the second reel "lost its animation" after power-on. */
    if (dt_ms > 50u) {
        dt_ms = 50u;
    }

    if (type == AnimationType::Spring) {
        update_spring(dt_ms);
    } else {
        update_easing(dt_ms);
    }
}

void AnimateValue::update_spring(uint32_t dt_ms) {
    float dt = dt_ms / 1000.0f;  // 转为秒
    /* dt already capped in update(); keep a soft guard for direct callers. */
    if (dt > 0.05f) dt = 0.05f;
    float displacement = _target - _current;

    // 检查是否接近静止
    if (std::abs(displacement) <= _spring.restDelta && std::abs(_spring_velocity) <= _spring.restSpeed) {
        _current = _target;
        _value = _current;
        _done = true;
        return;
    }

    // 弹簧力 F = k * x - c * v （x = target - current）
    float k = _spring.stiffness;
    float c = _spring.damping;
    float m = _spring.mass;

    float spring_force = k * displacement;
    float damping_force = -c * _spring_velocity;
    float acceleration = (spring_force + damping_force) / m;

    _spring_velocity += acceleration * dt;
    _current += _spring_velocity * dt;

    // 更新 value
    _value = _current;
}

void AnimateValue::update_easing(uint32_t dt_ms) {
    float duration = _easing.duration;
    if (duration <= 0.0f) {
        _current = _target;
        _value = _current;
        _done = true;
        return;
    }

    _easing_elapsed += dt_ms / 1000.0f;
    float progress = _easing_elapsed / duration;
    if (progress >= 1.0f) {
        _current = _target;
        _value = _current;
        _done = true;
    } else {
        float t = _easing.easing(progress);
        _current = _easing_start + (_target - _easing_start) * t;
        _value = _current;
    }
}

} // namespace lv_ui
