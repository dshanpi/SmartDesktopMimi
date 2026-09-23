/**
 * @file animate_value.hpp
 * @brief 动画值类型，支持 spring 和 easing 插值
 */
#pragma once
#include "animate_types.hpp"
#include "easing.hpp"
#include <functional>

namespace lv_ui {

struct SpringOptions {
    float stiffness = 180.0f;    // 刚度
    float damping = 12.0f;       // 阻尼
    float mass = 1.0f;          // 质量
    float velocity = 0.0f;      // 初始速度
    float restSpeed = 0.5f;     // 静止速度阈值
    float restDelta = 0.5f;     // 静止位置阈值
    float visualDuration = 0.3f; // 可视化动画时长（秒）
};

struct EasingOptions {
    float duration = 0.4f;     // 持续时间（秒）
    float (*easing)(float) = easing::ease_out_quad;
};

class AnimateValue {
public:
    AnimateValue() = default;
    AnimateValue(float initial) : _current(initial), _target(initial), _value(initial) {}

    // 赋值操作
    void setTarget(float target);
    void teleport(float value);

    // 更新（每帧调用）
    void update();

    // 动画完成判断
    bool done() const { return _done; }

    // 当前值
    float value() const { return _value; }

    // 直接值（不触发更新）
    float directValue() const { return _current; }

    // spring 参数
    SpringOptions& spring() { return _spring; }
    const SpringOptions& spring() const { return _spring; }

    // easing 参数
    EasingOptions& easing() { return _easing; }
    const EasingOptions& easing() const { return _easing; }

    // 动画类型
    AnimationType type = AnimationType::Spring;

private:
    float _current = 0.0f;
    float _target = 0.0f;
    float _value = 0.0f;
    bool _done = true;

    SpringOptions _spring;
    EasingOptions _easing;

    // spring 状态
    float _spring_velocity = 0.0f;
    float _last_tick = 0.0f;
    float _easing_start = 0.0f;
    float _easing_elapsed = 0.0f;

    // 内部更新
    void update_spring(uint32_t dt_ms);
    void update_easing(uint32_t dt_ms);
};

} // namespace lv_ui
