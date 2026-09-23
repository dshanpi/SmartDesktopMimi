#pragma once
#include "smooth_tk/spring.hpp"
#include "../../lvgl/lvgl.h"

namespace lv_ui {

class DockSpringSel {
public:
    static constexpr int MAX_BUTTONS = 7;
    static constexpr int SELECTOR_SLOT = 0;

    DockSpringSel(lv_obj_t* viewport, int btn_count, lv_coord_t btn_size, lv_coord_t btn_gap);
    ~DockSpringSel();

    void bind_button(int index, lv_obj_t* btn);
    void compute_slot_positions(lv_coord_t viewport_w, lv_coord_t viewport_h);

    void set_selected(int index, bool animated);
    int  get_selected() const { return _selected_idx; }

    void press();
    void release();

    void update();

private:
    struct ButtonAnim {
        lv_obj_t* obj = nullptr;
        smooth_ui_toolkit::Spring  pos_x;
        float  elapsed_pos = 0.0f;
        float  current_x   = 0.0f;
        bool   needs_write = false;
    };

    lv_obj_t*   _viewport;
    int         _btn_count;
    lv_coord_t  _btn_size;
    lv_coord_t  _btn_gap;
    int         _selected_idx = 0;
    lv_coord_t  _slot_x[MAX_BUTTONS]  = {0};
    lv_coord_t  _btn_y = 0;

    ButtonAnim  _buttons[MAX_BUTTONS];
    uint32_t    _last_time = 0;

    // LVGL 1/256 units: 256 = 1.0x
    static constexpr int32_t SCALE_SELECTED    = 300;
    static constexpr int32_t SCALE_UNSELECTED  = 242;
    static constexpr lv_opa_t OPA_SELECTED     = 255;
    static constexpr lv_opa_t OPA_UNSELECTED   = 179;

    // Spring tuning (position only)
    static constexpr float VD_POS   = 0.38f;
    static constexpr float BOUNCE_POS  = 0.30f;
    static constexpr lv_coord_t SELECTOR_X = 24;
    static constexpr lv_coord_t LABEL_RESERVED_H = 56;

    void _init_spring(smooth_ui_toolkit::Spring& sp, float vd, float bounce);
    void _apply_selection(bool animated);
    void _update_button(int i, float dt);
    void _write_button_lvgl(int i);
    static int _mod(int v, int m);
};

} // namespace lv_ui
