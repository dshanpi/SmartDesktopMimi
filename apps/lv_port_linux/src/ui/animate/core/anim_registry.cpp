#include "anim_registry.hpp"

#include <algorithm>
#include <vector>

namespace lv_ui {
namespace anim {

struct anim_entry_t {
    void *handle;
    anim_update_fn_t update_fn;
    anim_destroy_fn_t destroy_fn;
};

static std::vector<anim_entry_t> &registry()
{
    static std::vector<anim_entry_t> s_registry;
    return s_registry;
}

void register_anim(void *handle, anim_update_fn_t update_fn, anim_destroy_fn_t destroy_fn)
{
    std::vector<anim_entry_t> &r = registry();
    if (!handle || !update_fn) return;

    for (const auto &entry : r) {
        if (entry.handle == handle) return;
    }
    r.push_back({handle, update_fn, destroy_fn});
}

void unregister_anim(void *handle)
{
    std::vector<anim_entry_t> &r = registry();
    r.erase(std::remove_if(r.begin(), r.end(), [handle](const anim_entry_t &entry) {
                return entry.handle == handle;
            }),
            r.end());
}

void update_all(void)
{
    std::vector<anim_entry_t> &r = registry();
    for (const auto &entry : r) {
        entry.update_fn(entry.handle);
    }
}

void clear_all(void)
{
    std::vector<anim_entry_t> old_entries = registry();
    registry().clear();
    for (const auto &entry : old_entries) {
        if (entry.destroy_fn) entry.destroy_fn(entry.handle);
    }
}

} // namespace anim
} // namespace lv_ui
