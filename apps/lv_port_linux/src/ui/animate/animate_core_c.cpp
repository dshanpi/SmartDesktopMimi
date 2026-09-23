#include "animate_core_c.h"
#include "core/anim_registry.hpp"

void animate_update_all(void)
{
    lv_ui::anim::update_all();
}

void animate_clear_all(void)
{
    lv_ui::anim::clear_all();
}
