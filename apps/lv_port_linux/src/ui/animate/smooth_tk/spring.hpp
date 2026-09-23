#pragma once
#include "generators.hpp"

namespace smooth_ui_toolkit {

struct SpringOptions_t {
    float stiffness = 100.0;
    float damping = 10.0;
    float mass = 1.0;
    float velocity = 0.0;
    float restSpeed = 0.1;
    float restDelta = 0.1;
    float duration = 0.0;
    float bounce = 0.3;
    float visualDuration = 0.0;
};

class Spring : public KeyFrameGenerator {
public:
    Spring() {}
    ~Spring() {}

    SpringOptions_t springOptions;

    void setSpringOptions(float duration = 800.0f, float bounce = 0.3f, float visualDuration = 0.3f);
    inline void setSpringOptions(const SpringOptions_t& options)
    {
        springOptions = options;
    }

    virtual void init() override;
    virtual void retarget(float start, float end) override;
    virtual bool next(float t) override;
    virtual AnimationType type() const override
    {
        return AnimationType::Spring;
    }

protected:
    float _damping_ratio;
    float _undamped_angular_freq;
    float _current_velocity;

    enum class DampingType { Underdamped, Critical, Overdamped } _damping_type;

    float _sqrt_stiffness_mass;
    float _damped_angular_freq;
    float _initial_delta;

    float _velocity_c1, _velocity_c2;

    void calc_velocity(float t);
    void calc_velocity_analytical(float t);
    float calc_angular_freq(float undampedFreq, float dampingRatio);

    inline float calc_position(float t);
};

} // namespace smooth_ui_toolkit
