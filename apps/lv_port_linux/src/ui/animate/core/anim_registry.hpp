#pragma once

namespace lv_ui {
namespace anim {

typedef void (*anim_update_fn_t)(void *handle);
typedef void (*anim_destroy_fn_t)(void *handle);

void register_anim(void *handle, anim_update_fn_t update_fn, anim_destroy_fn_t destroy_fn);
void unregister_anim(void *handle);
void update_all(void);
void clear_all(void);

} // namespace anim
} // namespace lv_ui
