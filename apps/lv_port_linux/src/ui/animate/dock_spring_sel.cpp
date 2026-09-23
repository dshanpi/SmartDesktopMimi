#include "dock_spring_sel.hpp"
#include "animate_types.hpp"
#include "core/anim_registry.hpp"

namespace lv_ui {

/* lv_anim_t exec callbacks for scale and opacity */
static void _anim_scale_cb(void* obj, int32_t v) {
    lv_obj_set_style_transform_scale((lv_obj_t*)obj, v, 0);
}
static void _anim_opa_cb(void* obj, int32_t v) {
    lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
}

/* =======================
 *  Constructor / Destructor
 * ======================= */

DockSpringSel::DockSpringSel(lv_obj_t* viewport,
                             int btn_count,
                             lv_coord_t btn_size,
                             lv_coord_t btn_gap)
    : _viewport(viewport)
    , _btn_count(btn_count > MAX_BUTTONS ? MAX_BUTTONS : btn_count)
    , _btn_size(btn_size)
    , _btn_gap(btn_gap)
{
    for (int i = 0; i < _btn_count; i++) {
        _init_spring(_buttons[i].pos_x, VD_POS, BOUNCE_POS);
    }
    _last_time = hal_get_tick();

    anim::register_anim(this,
        [](void* h) { ((DockSpringSel*)h)->update(); },
        [](void* h) { delete (DockSpringSel*)h; });
}

DockSpringSel::~DockSpringSel()
{
    for (int i = 0; i < _btn_count; i++) {
        _buttons[i].obj = nullptr;
    }
}

/* =======================
 *  Initialization helpers
 * ======================= */

void DockSpringSel::_init_spring(smooth_ui_toolkit::Spring& sp, float vd, float bounce)
{
    sp.springOptions.visualDuration = vd;
    sp.springOptions.bounce = bounce;
    sp.springOptions.mass = 1.0f;
    sp.start = 0.0f;
    sp.end   = 0.0f;
    sp.init();
    sp.done = true;
}

void DockSpringSel::bind_button(int index, lv_obj_t* btn)
{
    if (index < 0 || index >= _btn_count || !btn) return;
    _buttons[index].obj = btn;
    lv_obj_set_style_transform_pivot_x(btn, _btn_size / 2, 0);
    lv_obj_set_style_transform_pivot_y(btn, _btn_size / 2, 0);
    lv_obj_set_style_transform_scale(btn, 256, 0);
}

void DockSpringSel::compute_slot_positions(lv_coord_t viewport_w, lv_coord_t viewport_h)
{
    (void)viewport_w;
    lv_coord_t start_x = SELECTOR_X;
    lv_coord_t usable_h = viewport_h - LABEL_RESERVED_H;
    if (usable_h < _btn_size) usable_h = _btn_size;
    _btn_y = (usable_h - _btn_size) / 2;

    for (int i = 0; i < _btn_count; i++) {
        _slot_x[i] = start_x + i * (_btn_size + _btn_gap);
    }
}

/* =======================
 *  Selection
 * ======================= */

void DockSpringSel::set_selected(int index, bool animated)
{
    if (index < 0 || index >= _btn_count) return;
    _selected_idx = index;
    _apply_selection(animated);
}

void DockSpringSel::_apply_selection(bool animated)
{
    for (int i = 0; i < _btn_count; i++) {
        ButtonAnim& ba = _buttons[i];
        if (!ba.obj) continue;

        int slot = _mod((i - _selected_idx) + SELECTOR_SLOT, _btn_count);
        float target_x = (float)_slot_x[slot];
        bool is_sel = (slot == SELECTOR_SLOT);
        int32_t target_scale = is_sel ? SCALE_SELECTED : SCALE_UNSELECTED;
        lv_opa_t target_opa  = is_sel ? OPA_SELECTED   : OPA_UNSELECTED;

        if (animated) {
            /* Position: spring with bounce */
            ba.pos_x.retarget(ba.current_x, target_x);
            ba.elapsed_pos = 0.0f;

            /* Scale: lv_anim_t ease-out (proven no-flash in old code) */
            lv_anim_t as;
            lv_anim_init(&as);
            lv_anim_set_var(&as, ba.obj);
            lv_anim_set_values(&as,
                lv_obj_get_style_transform_scale_x(ba.obj, LV_PART_MAIN),
                target_scale);
            lv_anim_set_time(&as, 250);
            lv_anim_set_path_cb(&as, lv_anim_path_ease_out);
            lv_anim_set_exec_cb(&as, _anim_scale_cb);
            lv_anim_start(&as);

            /* Opacity: lv_anim_t ease-out */
            lv_anim_t ao;
            lv_anim_init(&ao);
            lv_anim_set_var(&ao, ba.obj);
            lv_anim_set_values(&ao,
                (int32_t)lv_obj_get_style_opa(ba.obj, LV_PART_MAIN),
                (int32_t)target_opa);
            lv_anim_set_time(&ao, 200);
            lv_anim_set_path_cb(&ao, lv_anim_path_ease_out);
            lv_anim_set_exec_cb(&ao, _anim_opa_cb);
            lv_anim_start(&ao);
        } else {
            lv_obj_set_pos(ba.obj, (lv_coord_t)target_x, _btn_y);
            lv_obj_set_style_transform_scale(ba.obj, target_scale, 0);
            lv_obj_set_style_opa(ba.obj, target_opa, 0);
        }

        ba.current_x = target_x;
    }
}

/* =======================
 *  Press / Release
 * ======================= */

void DockSpringSel::press() {}
void DockSpringSel::release() {}

/* =======================
 *  Per-frame update (position spring only)
 * ======================= */

void DockSpringSel::update()
{
    uint32_t now = hal_get_tick();
    float dt = (float)(now - _last_time) / 1000.0f;
    _last_time = now;

    if (dt <= 0.0f) return;
    if (dt > 0.05f) dt = 0.05f;

    for (int i = 0; i < _btn_count; i++) {
        _update_button(i, dt);
        if (_buttons[i].needs_write) {
            _write_button_lvgl(i);
            _buttons[i].needs_write = false;
        }
    }
}

void DockSpringSel::_update_button(int i, float dt)
{
    ButtonAnim& ba = _buttons[i];
    if (!ba.obj) return;

    if (!ba.pos_x.done) {
        ba.elapsed_pos += dt;
        ba.pos_x.next(ba.elapsed_pos);
        ba.current_x = ba.pos_x.done ? ba.pos_x.end : ba.pos_x.value;
        ba.needs_write = true;
    }
}

void DockSpringSel::_write_button_lvgl(int i)
{
    ButtonAnim& ba = _buttons[i];
    if (!ba.obj) return;
    lv_obj_set_pos(ba.obj, (lv_coord_t)ba.current_x, _btn_y);
}

/* =======================
 *  Utility
 * ======================= */

int DockSpringSel::_mod(int v, int m)
{
    int r = v % m;
    return (r < 0) ? (r + m) : r;
}

} // namespace lv_ui
