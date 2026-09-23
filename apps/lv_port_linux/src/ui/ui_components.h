#ifndef UI_COMPONENTS_H
#define UI_COMPONENTS_H

#include "../../lvgl/lvgl.h"

/*****************************************************************************
 *                          共享 UI 组件
 *****************************************************************************/

/* 创建标准 app 顶栏（返回 header 对象）。
 *
 * 统一替换各 app 里重复的 top_bar / back_btn / title / right_spacer 创建代码。
 *
 * @param parent        父对象（通常是 app 屏幕 scr）
 * @param title         居中标题文字
 * @param back_cb       返回按钮点击回调（LV_EVENT_CLICKED）；可为 NULL
 * @param allow_compact 是否按屏幕分辨率自适应 compact 模式（setting/bt 用 true；
 *                      wifi/ai/ota 用 false 保持固定标准尺寸）
 * @param title_font    标题字体；NULL 时按 compact 选 ui_font_h3()/ui_font_h2()
 *
 * 布局：透明背景 + 底边框 + 横向 flex；
 *       左 back_btn（透明，arrow_back 图标）+ 居中 title（flex_grow，省略）+ 右 spacer。
 *
 * 顶栏实际高度可通过 ui_app_header_height(allow_compact) 查询，供调用方定位内容区。
 */
lv_obj_t * ui_create_app_header(lv_obj_t *parent,
                                const char *title,
                                lv_event_cb_t back_cb,
                                bool allow_compact,
                                const lv_font_t *title_font);

/* 计算 app 顶栏高度（allow_compact 语义同 ui_create_app_header）。
 * allow_compact=false 时恒为 UI_APP_HEADER_H；true 时按屏幕分辨率返回 compact 或标准高度。*/
lv_coord_t ui_app_header_height(bool allow_compact);

#endif /* UI_COMPONENTS_H */
