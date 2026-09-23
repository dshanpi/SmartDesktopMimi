#pragma once
#include "../../lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void* dock_spring_sel_create(void* viewport,
                              int btn_count,
                              lv_coord_t btn_size,
                              lv_coord_t btn_gap);

void  dock_spring_sel_bind_button(void* handle, int index, void* btn);

void  dock_spring_sel_compute_layout(void* handle,
                                      lv_coord_t viewport_w,
                                      lv_coord_t viewport_h);

void  dock_spring_sel_set_selected(void* handle, int index, int animated);

int   dock_spring_sel_get_selected(void* handle);

void  dock_spring_sel_press(void* handle);
void  dock_spring_sel_release(void* handle);

void  dock_spring_sel_delete(void* handle);

#ifdef __cplusplus
}
#endif
