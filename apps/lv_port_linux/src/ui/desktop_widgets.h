#ifndef MIMI_DESKTOP_WIDGETS_H
#define MIMI_DESKTOP_WIDGETS_H
#include "../../lvgl/lvgl.h"
#include "desktop_widget_layout.h"
#include "../middleware/middleware.h"
typedef lv_obj_t * (*mimi_widget_factory)(lv_obj_t *, mimi_widget_kind, int, int);
void desktop_widgets_create(lv_obj_t * parent, mimi_widget_factory factory);
void desktop_widgets_edit(void);
void desktop_widgets_sensor(bool valid, int temperature, int humidity);
void desktop_widgets_pairing(const mw_msg_t * msg);
void desktop_widgets_cloud(int state);
void desktop_widgets_network(void);
const char * desktop_widgets_connection_text(void);
#endif
