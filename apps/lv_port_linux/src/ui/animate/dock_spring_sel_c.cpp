#include "dock_spring_sel_c.h"
#include "dock_spring_sel.hpp"
#include "core/anim_registry.hpp"

void* dock_spring_sel_create(void* viewport,
                              int btn_count,
                              lv_coord_t btn_size,
                              lv_coord_t btn_gap)
{
    auto* d = new lv_ui::DockSpringSel((lv_obj_t*)viewport, btn_count, btn_size, btn_gap);
    return d;
}

void dock_spring_sel_bind_button(void* handle, int index, void* btn)
{
    if (handle) ((lv_ui::DockSpringSel*)handle)->bind_button(index, (lv_obj_t*)btn);
}

void dock_spring_sel_compute_layout(void* handle,
                                     lv_coord_t viewport_w,
                                     lv_coord_t viewport_h)
{
    if (handle) ((lv_ui::DockSpringSel*)handle)->compute_slot_positions(viewport_w, viewport_h);
}

void dock_spring_sel_set_selected(void* handle, int index, int animated)
{
    if (handle) ((lv_ui::DockSpringSel*)handle)->set_selected(index, (bool)animated);
}

int dock_spring_sel_get_selected(void* handle)
{
    return handle ? ((lv_ui::DockSpringSel*)handle)->get_selected() : 0;
}

void dock_spring_sel_press(void* handle)
{
    if (handle) ((lv_ui::DockSpringSel*)handle)->press();
}

void dock_spring_sel_release(void* handle)
{
    if (handle) ((lv_ui::DockSpringSel*)handle)->release();
}

void dock_spring_sel_delete(void* handle)
{
    if (handle) {
        lv_ui::anim::unregister_anim(handle);
        delete (lv_ui::DockSpringSel*)handle;
    }
}
