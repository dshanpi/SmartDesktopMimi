/**
 * @file animate_types.hpp
 * @brief 动画工具库公共类型定义
 */
#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace lv_ui {

/**
 * @brief 获取自系统启动以来的毫秒数
 */
uint32_t hal_get_tick();

/**
 * @brief 延时（毫秒）
 */
void hal_delay(uint32_t ms);

/**
 * @brief 公共数学工具
 */
namespace math {
template <typename T>
constexpr T clamp(T val, T min_val, T max_val) {
    return val < min_val ? min_val : (val > max_val ? max_val : val);
}

template <typename T, typename T2>
constexpr T min(T a, T2 b) {
    return a < b ? a : b;
}

template <typename T, typename T2>
constexpr T max(T a, T2 b) {
    return a > b ? a : b;
}
}

/**
 * @brief 动画类型
 */
enum class AnimationType {
    Spring,
    Easing
};

} // namespace lv_ui
