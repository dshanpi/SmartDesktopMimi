#include "wifi_app.h"
#include "../../ui/theme/theme.h"
#include "../../ui/ui_components.h"
#include "../../ui/ui_i18n.h"
#include "../../ui/mimi_ui.h"
#include "../../system/backend_service.h"
#include "../../system/app_manager.h"
#include "../../system/settings.h"
#include "../../middleware/middleware.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Resource Declarations */
LV_IMG_DECLARE(wifi_off_28dp_666666)      // WiFi Off / Weak Signal
LV_IMG_DECLARE(refresh_28dp_1F1F1F)       // Refresh Icon
LV_IMG_DECLARE(lock_outline_28dp_666666)  // Lock Icon

/* UI Components */
static lv_obj_t * wifi_screen = NULL;
static lv_obj_t * wifi_list = NULL;
static lv_obj_t * spinner = NULL;
static lv_obj_t * scan_info_cont = NULL;
static lv_obj_t * sw_wlan = NULL;
static lv_obj_t * wlan_pending_label = NULL;

/* WiFi page typography: SarasaUiSC when available, with CJK fallback. */
#define WIFI_FONT_TITLE      UI_TEXT_H2
#define WIFI_FONT_SECTION    UI_TEXT_H2
#define WIFI_FONT_BODY       UI_TEXT_BODY_LG
#define WIFI_FONT_META       UI_TEXT_BODY_MD

#define WIFI_STATUS_CONNECTED ui_tr(UI_STR_CONNECTED)

/* Data Types */
typedef struct {
    char * ssid;
    int rssi; // 0-4
    bool encrypted;
    bool connected;
} wifi_ap_t;

/* State Variables */
static bool wifi_app_active = false;
static bool is_scanning = false;
static bool is_wifi_enabled = true; // Default to enabled, could persist
static bool ignore_switch_event = false;
static bool wifi_settings_dirty = false;
static bool wlan_toggle_pending = false;
static bool wlan_toggle_target_enabled = false;
static bool runtime_status_received = false;
static bool startup_scan_requested = false;

/* Modal Components */
static lv_obj_t * password_modal = NULL;
static lv_obj_t * password_ta = NULL;
static lv_obj_t * password_kb = NULL;
static lv_obj_t * password_error_label = NULL;

static void close_modal_cb(lv_event_t * e);
static void connect_click_cb(lv_event_t * e);
static void keyboard_event_cb(lv_event_t * e);

/* Forward Declarations */
static void wifi_list_item_event_cb(lv_event_t * e);
static lv_obj_t * create_list_item(lv_obj_t * parent, wifi_ap_t * ap);
static void scan_click_cb(lv_event_t * e);
static void wlan_switch_event_cb(lv_event_t * e);
static lv_obj_t * wifi_item_create(lv_obj_t * parent, const char * name, const char * status, bool locked);
static void back_event_handler(lv_event_t * e);
static void wifi_runtime_handler(const mw_msg_t * msg);
static void wifi_spinner_show(void);
static void wifi_spinner_delete(void);

static void wifi_spinner_delete(void) {
    if (spinner) {
        lv_obj_delete(spinner);
        spinner = NULL;
    }
}

static void wifi_spinner_show(void) {
    if (!wifi_app_active || !wifi_screen || spinner) return;

    spinner = lv_spinner_create(wifi_screen);
    lv_obj_set_size(spinner, 100, 100);
    lv_obj_center(spinner);
    lv_obj_set_style_arc_color(spinner, mimi_ui_color(MIMI_TEAL), LV_PART_INDICATOR);
    lv_obj_move_foreground(spinner);
}

static void wifi_status_handler(const mw_msg_t * msg) {
    if (!wifi_app_active || !wifi_list) return;

    if (msg->topic == TOPIC_WIFI_STATUS && msg->data_len == sizeof(wifi_ap_info_t)) {
        wifi_ap_info_t * info = (wifi_ap_info_t *)msg->data;

        // 检查 SSID 是否有效
        if (!info->ssid || strlen(info->ssid) == 0) {
            return;
        }

        // 检查是否是扫描完成标记
        if (strcmp(info->ssid, "__SCAN_COMPLETE__") == 0) {
            // 隐藏 spinner，标记扫描完成
            wifi_spinner_delete();
            is_scanning = false;
            if (wifi_list && lv_obj_get_child_count(wifi_list) == 0) {
                lv_obj_t *empty = lv_label_create(wifi_list);
                lv_label_set_text(empty, ui_tr(UI_STR_NO_NETWORKS));
                lv_obj_set_width(empty, lv_pct(100));
                lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
                lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_set_style_text_font(empty, WIFI_FONT_BODY, 0);
                lv_obj_set_style_text_color(empty, mimi_ui_color(MIMI_MUTED), 0);
                lv_obj_set_style_pad_top(empty, 48, 0);
            }
            printf("[WiFi] Scan complete\n");
            return; // 不添加到列表
        }

        // 检查是否是断开连接标记
        if (strcmp(info->ssid, "__DISCONNECTED__") == 0) {
            printf("[WiFi] Disconnected\n");
            /* 遍历列表清除连接状态 */
            uint32_t cnt = lv_obj_get_child_cnt(wifi_list);
            for (uint32_t i = 0; i < cnt; i++) {
                lv_obj_t * item = lv_obj_get_child(wifi_list, i);
                lv_obj_t * right_cont = lv_obj_get_child(item, 1);
                if (right_cont) {
                    uint32_t right_cnt = lv_obj_get_child_cnt(right_cont);
                    for (uint32_t j = 0; j < right_cnt; j++) {
                        lv_obj_t * child = lv_obj_get_child(right_cont, j);
                        if (lv_obj_check_type(child, &lv_label_class)) {
                            lv_label_set_text(child, "");
                            lv_obj_update_layout(right_cont); // 显式更新布局
                        } else if (lv_obj_check_type(child, &lv_image_class)) {
                            // 恢复锁图标（如果存在）
                            lv_obj_clear_flag(child, LV_OBJ_FLAG_HIDDEN);
                        }
                    }
                }
            }
            return; 
        }

        /* WiFi 关闭后忽略在途 AP/连接更新，避免列表被旧扫描结果重新刷出来 */
        if (!is_wifi_enabled) {
            return;
        }

        /* 
         * 扫描状态处理：
         * 我们不再在收到第一个 AP 时删除 spinner，而是等待 __SCAN_COMPLETE__。
         * 这段逻辑保留是为了说明我们特意改变了行为。
         */
        if (is_scanning) {
            // 什么都不做，保持 spinner 显示
        }

        // 查找是否已存在该 SSID
    lv_obj_t * existing_item = NULL;
    uint32_t cnt = lv_obj_get_child_cnt(wifi_list);
    for (uint32_t i = 0; i < cnt; i++) {
        lv_obj_t * item = lv_obj_get_child(wifi_list, i);
        
        // FIX: 现在 label_name 在 left_cont (child 0) 里面
        lv_obj_t * left_cont = lv_obj_get_child(item, 0);
        if (left_cont) {
            // 假设 label 是 left_cont 的第一个 child
            lv_obj_t * label_name = lv_obj_get_child(left_cont, 0);
            if (label_name && lv_obj_check_type(label_name, &lv_label_class)) {
                const char * txt = lv_label_get_text(label_name);
                if (txt && strcmp(txt, info->ssid) == 0) {
                    existing_item = item;
                    break;
                }
            }
        }
    }

    // 如果是已连接状态，且发现已有同名项目，但它不是第一个
    // 或者发现列表中已经有其他项目显示已连接，但不是当前这个
    // 我们需要清理其他的已连接状态
    if (info->connected) {
        for (uint32_t i = 0; i < cnt; i++) {
            lv_obj_t * item = lv_obj_get_child(wifi_list, i);
            if (item == existing_item) continue; // 跳过当前正在更新的这个

            lv_obj_t * right_cont = lv_obj_get_child(item, 1);
            if (right_cont) {
                uint32_t right_cnt = lv_obj_get_child_cnt(right_cont);
                for (uint32_t j = 0; j < right_cnt; j++) {
                    lv_obj_t * child = lv_obj_get_child(right_cont, j);
                    if (lv_obj_check_type(child, &lv_label_class)) {
                        // 如果其他项目显示了已连接，清除它
                        const char * txt = lv_label_get_text(child);
                        if (txt && strcmp(txt, WIFI_STATUS_CONNECTED) == 0) {
                            lv_label_set_text(child, "");
                            // 显式更新布局
                            lv_obj_update_layout(right_cont);
                        }
                    } else if (lv_obj_check_type(child, &lv_image_class)) {
                         // 重新显示锁
                         lv_obj_clear_flag(child, LV_OBJ_FLAG_HIDDEN);
                    }
                }
            }
        }
    }

    if (existing_item) {
            // 更新状态
            // Child 1 is the 'right' container, we need to find the label inside it
            lv_obj_t * right_cont = lv_obj_get_child(existing_item, 1);
            if (right_cont) {
                // 如果是已连接状态，我们需要确保它排在第一位
                if (info->connected) {
                    lv_obj_move_to_index(existing_item, 0);
                    
                    /*
                     * FIX: 移除这里的自动 CMD_SCAN。
                     * 原因：后端在连接成功后，已经自动调用了 service_wifi_scan()。
                     * 我们只需要等待后端的扫描结果即可，不需要重复触发，避免竞态。
                     */
                }

                uint32_t right_cnt = lv_obj_get_child_cnt(right_cont);
                for (uint32_t j = 0; j < right_cnt; j++) {
                    lv_obj_t * child = lv_obj_get_child(right_cont, j);
                    if (lv_obj_check_type(child, &lv_label_class)) {
                        lv_label_set_text(child, info->connected ? WIFI_STATUS_CONNECTED : "");
                        // 显式调用布局更新，防止文字遮挡
                        lv_obj_update_layout(right_cont); 
                        lv_obj_update_layout(existing_item);
                    } else if (lv_obj_check_type(child, &lv_image_class)) {
                        if (info->connected) {
                            lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
                        } else {
                            lv_obj_clear_flag(child, LV_OBJ_FLAG_HIDDEN);
                        }
                    }
                }
            }
            
            // 更新 user_data 中的 connected 状态 (有点麻烦，因为 user_data 挂在 event 上)
            // 这里我们主要关注 UI 显示
        } else {
            // 创建新项
            wifi_ap_t ap;
            ap.ssid = strdup(info->ssid); 
            ap.rssi = info->rssi;
            ap.encrypted = info->encrypted;
            ap.connected = info->connected;
            lv_obj_t * new_item = create_list_item(wifi_list, &ap);
            
            // 如果新创建的项是已连接状态，将其移到第一位
            if (info->connected) {
                lv_obj_move_to_index(new_item, 0);
                
                /* FIX: 同样移除这里的自动 CMD_SCAN */
            }
        }
    }
}

static void wifi_runtime_handler(const mw_msg_t * msg)
{
    if (!msg || msg->topic != TOPIC_WIFI_RUNTIME || msg->data_len != sizeof(wifi_runtime_status_t)) return;
    const wifi_runtime_status_t *runtime = (const wifi_runtime_status_t *)msg->data;
    sys_settings_t *settings = sys_settings_get();
    bool first_runtime_status = !runtime_status_received;

    runtime_status_received = true;
    is_wifi_enabled = runtime->enabled;
    is_scanning = runtime->scanning;

    if (!wifi_app_active || !wifi_list) return;

    if (sw_wlan) {
        bool checked = lv_obj_has_state(sw_wlan, LV_STATE_CHECKED);
        if (checked != runtime->enabled) {
            ignore_switch_event = true;
            if (runtime->enabled) lv_obj_add_state(sw_wlan, LV_STATE_CHECKED);
            else lv_obj_clear_state(sw_wlan, LV_STATE_CHECKED);
            ignore_switch_event = false;
        }
    }

    if (scan_info_cont) {
        if (runtime->enabled) lv_obj_clear_flag(scan_info_cont, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(scan_info_cont, LV_OBJ_FLAG_HIDDEN);
    }

    if (!runtime->enabled) {
        lv_obj_clean(wifi_list);
        wifi_spinner_delete();
    } else if (runtime->scanning) {
        wifi_spinner_show();
    } else {
        wifi_spinner_delete();
    }

    if (wlan_toggle_pending && runtime->last_error_code != 0) {
        wlan_toggle_pending = false;
        if (sw_wlan) {
            lv_obj_clear_state(sw_wlan, LV_STATE_DISABLED);
        }
        if (wlan_pending_label) {
            lv_label_set_text(wlan_pending_label, ui_tr(UI_STR_FAILED));
        }
        printf("[WiFi] Runtime error: %d\n", runtime->last_error_code);
    } else if (wlan_toggle_pending && runtime->enabled == wlan_toggle_target_enabled) {
        wlan_toggle_pending = false;
        if (settings->wifi_enabled != runtime->enabled) {
            settings->wifi_enabled = runtime->enabled;
            wifi_settings_dirty = true;
        }
        if (sw_wlan) {
            lv_obj_clear_state(sw_wlan, LV_STATE_DISABLED);
        }
        if (wlan_pending_label) {
            lv_label_set_text(wlan_pending_label, "");
        }
    } else if (!wlan_toggle_pending && runtime->last_error_code == 0) {
        if (wlan_pending_label) {
            lv_label_set_text(wlan_pending_label, "");
        }
    }

    /* The persisted preference can lag behind a driver that recovered after
     * boot. Once the page has the backend's authoritative state, restore the
     * same one-shot scan the page normally performs on entry. */
    if (first_runtime_status && runtime->enabled && !runtime->scanning &&
        !startup_scan_requested) {
        scan_click_cb(NULL);
    }
}

static void free_wifi_ap_data(lv_event_t * e) {
    wifi_ap_t * ap = (wifi_ap_t *)lv_event_get_user_data(e);
    if (ap && ap->ssid) {
        free(ap->ssid);
        free(ap);
    }
}

static lv_obj_t * create_list_item(lv_obj_t * parent, wifi_ap_t * ap) {
    lv_obj_t * item = wifi_item_create(parent, ap->ssid, ap->connected ? WIFI_STATUS_CONNECTED : "", ap->encrypted);
    
    // Bind data to click event
    // Note: We need to copy ap data because stack variable will be gone
    // We reuse wifi_ap_t structure but we need to manage memory
    wifi_ap_t * ap_copy = malloc(sizeof(wifi_ap_t));
    if (ap_copy) {
        ap_copy->ssid = strdup(ap->ssid);
        ap_copy->rssi = ap->rssi;
        ap_copy->encrypted = ap->encrypted;
        ap_copy->connected = ap->connected;
        
        lv_obj_add_event_cb(item, wifi_list_item_event_cb, LV_EVENT_CLICKED, ap_copy);
        lv_obj_add_event_cb(item, free_wifi_ap_data, LV_EVENT_DELETE, ap_copy); // Clean up user_data on delete
    }
    return item;
}

/* Re-add missing function implementations that were accidentally removed or duplicated */

static void close_modal_cb(lv_event_t * e) {
    LV_UNUSED(e);
    if (password_modal) {
        lv_obj_delete(password_modal);
        password_modal = NULL;
        password_kb = NULL;
        password_ta = NULL;
        password_error_label = NULL;
    }
}

static void connect_click_cb(lv_event_t * e) {
    const char * ssid = (const char *)lv_event_get_user_data(e);
    if (password_ta) {
        const char * password = lv_textarea_get_text(password_ta);
        if (strlen(password) < 8) {
            if (password_error_label) {
                lv_label_set_text(password_error_label,
                                  ui_tr(UI_STR_WIFI_PASSWORD_SHORT));
                lv_obj_set_style_text_color(password_error_label,
                                            UI_COLOR_ERROR, 0);
            }
            return;
        }
        printf("Connecting to %s\n", ssid);
        
        // 发送连接指令到后端
        wifi_cmd_t cmd = {0};
        cmd.action = CMD_CONNECT;
        snprintf(cmd.ssid, sizeof(cmd.ssid), "%s", ssid);
        snprintf(cmd.password, sizeof(cmd.password), "%s", password);
        mw_publish(TOPIC_WIFI_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
    }
    close_modal_cb(e);
}

static void keyboard_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_READY) {
        const char * ssid = (const char *)lv_event_get_user_data(e);
        if (password_ta) {
            const char * password = lv_textarea_get_text(password_ta);
            if (strlen(password) < 8) {
                if (password_error_label) {
                    lv_label_set_text(password_error_label,
                                      ui_tr(UI_STR_WIFI_PASSWORD_SHORT));
                    lv_obj_set_style_text_color(password_error_label,
                                                UI_COLOR_ERROR, 0);
                }
                return;
            }
            printf("Keyboard OK: Connecting to %s\n", ssid);
            
            // 发送连接指令到后端
            wifi_cmd_t cmd = {0};
            cmd.action = CMD_CONNECT;
            snprintf(cmd.ssid, sizeof(cmd.ssid), "%s", ssid);
            snprintf(cmd.password, sizeof(cmd.password), "%s", password);
            mw_publish(TOPIC_WIFI_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
            
            /* Keep the known list visible while the backend associates. */
            wifi_spinner_delete();
            wifi_spinner_show();
            is_scanning = true; // 复用扫描逻辑
        }
        close_modal_cb(NULL); 
    } else if (code == LV_EVENT_CANCEL) {
        close_modal_cb(NULL);
    }
}

static void free_modal_data(lv_event_t * e) {
    char * ssid = (char *)lv_event_get_user_data(e);
    if (ssid) {
        free(ssid);
    }
}

static void show_password_event_cb(lv_event_t * e) {
    lv_obj_t * cb = lv_event_get_target(e);
    bool checked = lv_obj_has_state(cb, LV_STATE_CHECKED);
    if (password_ta) {
        lv_textarea_set_password_mode(password_ta, !checked);
    }
}

static void wifi_list_item_event_cb(lv_event_t * e) {
    wifi_ap_t * ap = (wifi_ap_t *)lv_event_get_user_data(e);
    lv_obj_t * item = lv_event_get_target(e);

    /*
     * FIX: 通过实时检查 UI 状态来判断连接状态，而不是依赖 user_data (ap->connected)。
     * 因为 ap->connected 在 create 后就不更新了，而 UI 状态是实时的。
     */
    bool is_actually_connected = false;
    lv_obj_t * right_cont = lv_obj_get_child(item, 1);
    if (right_cont) {
        uint32_t right_cnt = lv_obj_get_child_cnt(right_cont);
        for (uint32_t j = 0; j < right_cnt; j++) {
            lv_obj_t * child = lv_obj_get_child(right_cont, j);
            if (lv_obj_check_type(child, &lv_label_class)) {
                const char * txt = lv_label_get_text(child);
                if (txt && strcmp(txt, WIFI_STATUS_CONNECTED) == 0) {
                    is_actually_connected = true;
                }
                break;
            }
        }
    }

    // 如果当前 UI 显示为已连接，则拦截点击，不允许重复输入密码
    if (is_actually_connected) {
        printf("Item %s is already connected, ignoring click.\n", ap->ssid);
        return;
    }

    if (!ap->encrypted) {
        wifi_cmd_t cmd = {0};
        cmd.action = CMD_CONNECT;
        snprintf(cmd.ssid, sizeof(cmd.ssid), "%s", ap->ssid);
        mw_publish(TOPIC_WIFI_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
        if (right_cont) {
            uint32_t count = lv_obj_get_child_count(right_cont);
            for (uint32_t i = 0; i < count; i++) {
                lv_obj_t *child = lv_obj_get_child(right_cont, i);
                if (lv_obj_check_type(child, &lv_label_class))
                    lv_label_set_text(child, ui_tr(UI_STR_CONNECTING));
            }
        }
        return;
    }

    // 否则（包括曾连接但现已断开的），允许弹出密码框重新连接

    // Create Full Screen Modal Overlay
    // lv_obj_create inherits the default theme radius; force a square full-screen
    // scrim so the four corners don't show rounded cutouts over the WiFi page.
    password_modal = lv_obj_create(lv_scr_act());
    lv_obj_set_size(password_modal, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(password_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(password_modal, LV_OPA_50, 0); // Semi-transparent
    lv_obj_set_style_radius(password_modal, 0, 0);
    lv_obj_set_style_border_width(password_modal, 0, 0);
    lv_obj_set_style_outline_width(password_modal, 0, 0);
    lv_obj_set_style_shadow_width(password_modal, 0, 0);
    lv_obj_set_style_pad_all(password_modal, 0, 0);
    lv_obj_clear_flag(password_modal, LV_OBJ_FLAG_SCROLLABLE);

    // Persist SSID for the modal lifecycle
    char * ssid_copy = strdup(ap->ssid);
    lv_obj_add_event_cb(password_modal, free_modal_data, LV_EVENT_DELETE, ssid_copy);

    lv_obj_t *dialog = lv_obj_create(password_modal);
    mimi_ui_surface(dialog, 18);
    int32_t screen_w = lv_display_get_horizontal_resolution(NULL);
    int32_t screen_h = lv_display_get_vertical_resolution(NULL);
    lv_obj_set_size(dialog, LV_MIN(820, screen_w - 48), screen_h - 48);
    lv_obj_center(dialog);
    lv_obj_set_style_pad_all(dialog, 24, 0);
    lv_obj_set_style_pad_row(dialog, 12, 0);
    lv_obj_set_flex_flow(dialog, LV_FLEX_FLOW_COLUMN);

    // Title
    lv_obj_t * title = lv_label_create(dialog);
    lv_label_set_text_fmt(title,
                          ui_i18n_is_zh() ? "连接到 %s" : "Connect to %s",
                          ssid_copy);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(title, WIFI_FONT_SECTION, 0);
    lv_obj_set_style_text_color(title, mimi_ui_color(MIMI_TEXT), 0);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(title, LV_PCT(100));

    // Password Input
    password_ta = lv_textarea_create(dialog);
    lv_textarea_set_password_mode(password_ta, true);
    lv_textarea_set_password_bullet(password_ta, "*"); // Use asterisk if bullet is missing
    lv_textarea_set_one_line(password_ta, true);
    lv_textarea_set_placeholder_text(password_ta, ui_tr(UI_STR_WIFI_PASSWORD));
    lv_textarea_set_max_length(password_ta, 63);
    lv_obj_set_width(password_ta, LV_PCT(100));
    lv_obj_set_style_bg_color(password_ta, mimi_ui_color(MIMI_SURFACE_ALT), 0);
    lv_obj_set_style_text_color(password_ta, mimi_ui_color(MIMI_TEXT), 0);
    lv_obj_set_style_text_color(password_ta, mimi_ui_color(MIMI_MUTED), LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_color(password_ta, mimi_ui_color(MIMI_BORDER), 0);
    lv_obj_set_height(password_ta, 60);
    lv_obj_set_style_text_font(password_ta, WIFI_FONT_BODY, 0);
    lv_obj_set_style_radius(password_ta, 12, 0);

    // Show Password Checkbox
    lv_obj_t * cb_show_pass = lv_checkbox_create(dialog);
    lv_obj_set_style_min_height(cb_show_pass, 48, 0);
    lv_obj_set_style_pad_ver(cb_show_pass, 10, 0);
    lv_checkbox_set_text(cb_show_pass, ui_tr(UI_STR_SHOW_PASSWORD));
    lv_obj_set_style_text_font(cb_show_pass, WIFI_FONT_META, 0);
    lv_obj_set_style_text_color(cb_show_pass, mimi_ui_color(MIMI_MUTED), 0);
    // Apply theme color to indicator
    lv_obj_set_style_bg_color(cb_show_pass, mimi_ui_color(MIMI_TEAL), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_event_cb(cb_show_pass, show_password_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    password_error_label = lv_label_create(dialog);
    lv_label_set_text(password_error_label, "");
    lv_obj_set_height(password_error_label, 28);
    lv_obj_set_width(password_error_label, lv_pct(100));
    lv_obj_set_style_text_font(password_error_label, WIFI_FONT_META, 0);

    // Buttons Container
    lv_obj_t * btn_cont = lv_obj_create(dialog);
    lv_obj_set_width(btn_cont, LV_PCT(100));
    lv_obj_set_height(btn_cont, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(btn_cont, 0, 0);

    // Cancel Button
    lv_obj_t * btn_cancel = lv_button_create(btn_cont);
    lv_obj_set_size(btn_cancel, 200, 52);
    mimi_ui_button_style(btn_cancel, false);
    lv_obj_add_event_cb(btn_cancel, close_modal_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, ui_tr(UI_STR_CANCEL));
    lv_obj_set_style_text_font(lbl_cancel, WIFI_FONT_BODY, 0);
    lv_obj_center(lbl_cancel);

    // Connect Button
    lv_obj_t * btn_connect = lv_button_create(btn_cont);
    lv_obj_set_size(btn_connect, 200, 52);
    mimi_ui_button_style(btn_connect, true);
    lv_obj_add_event_cb(btn_connect, connect_click_cb, LV_EVENT_CLICKED, (void*)ssid_copy);
    lv_obj_t * lbl_connect = lv_label_create(btn_connect);
    lv_label_set_text(lbl_connect, ui_tr(UI_STR_CONNECT));
    lv_obj_set_style_text_font(lbl_connect, WIFI_FONT_BODY, 0);
    lv_obj_center(lbl_connect);

    password_kb = lv_keyboard_create(dialog);
    mimi_ui_keyboard_style(password_kb);
    lv_obj_set_width(password_kb, lv_pct(100));
    lv_obj_set_height(password_kb, 1);
    lv_obj_set_flex_grow(password_kb, 1);
    lv_keyboard_set_mode(password_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(password_kb, password_ta);
    lv_obj_add_event_cb(password_kb, keyboard_event_cb, LV_EVENT_ALL, (void *)ssid_copy);

}

static void scan_click_cb(lv_event_t * e) {
    LV_UNUSED(e);
    if (!wifi_app_active || !wifi_list || is_scanning || !is_wifi_enabled) return; // Don't scan if WiFi is off

    startup_scan_requested = true;

    /* Keep known networks visible while the backend refreshes them in place. */
    wifi_spinner_delete();
    wifi_spinner_show();
    
    is_scanning = true;

    // Send Scan Command via Middleware
    wifi_cmd_t cmd = {0};
    cmd.action = CMD_SCAN;
    mw_publish(TOPIC_WIFI_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
}

static void wlan_switch_event_cb(lv_event_t * e) {
    if (ignore_switch_event) return;
    if (wlan_toggle_pending) return;
    lv_obj_t * sw = lv_event_get_target(e);
    wifi_cmd_t cmd = {0};
    is_wifi_enabled = lv_obj_has_state(sw, LV_STATE_CHECKED);
    wlan_toggle_target_enabled = is_wifi_enabled;
    wlan_toggle_pending = true;

    if (sw_wlan) {
        lv_obj_add_state(sw_wlan, LV_STATE_DISABLED);
    }
    if (wlan_pending_label) {
        lv_label_set_text(wlan_pending_label, ui_tr(UI_STR_CONNECTING));
    }
    
    if (is_wifi_enabled) {
        cmd.action = CMD_ENABLE;
        mw_publish(TOPIC_WIFI_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
        // Enable WiFi: backend will auto-scan after enabled
        if (scan_info_cont) lv_obj_clear_flag(scan_info_cont, LV_OBJ_FLAG_HIDDEN);
        if (wifi_list) lv_obj_clean(wifi_list);
        wifi_spinner_delete();
        wifi_spinner_show();
        is_scanning = true;
    } else {
        cmd.action = CMD_DISABLE;
        mw_publish(TOPIC_WIFI_COMMAND, &cmd, sizeof(cmd), MW_DIR_UI_TO_BACKEND);
        // Disable WiFi: Clear list and hide controls
        if (wifi_list) lv_obj_clean(wifi_list);
        wifi_spinner_delete();
        is_scanning = false;
        if (scan_info_cont) lv_obj_add_flag(scan_info_cont, LV_OBJ_FLAG_HIDDEN);
        // Optionally send disable command to backend
    }
}

static void back_event_handler(lv_event_t * e) {
    LV_UNUSED(e);
    // Implement back navigation or close logic
    printf("Back button clicked\n");
    app_manager_back_home();
}

lv_obj_t * wifi_item_create(lv_obj_t * parent, const char * name, const char * status, bool locked) {
    lv_obj_t * item = lv_obj_create(parent);
    lv_obj_set_width(item, lv_pct(100));
    mimi_ui_surface(item, 0);
    lv_obj_set_height(item, 90);
    lv_obj_set_style_border_side(item, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_bg_color(item, mimi_ui_color(MIMI_SURFACE), 0);
    lv_obj_set_style_radius(item, 0, 0);
    lv_obj_set_style_border_width(item, 1, 0);
    // 移除 item 这一层的 Flex 布局，改用手动管理子容器
    // lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW); // 移除
    // lv_obj_set_flex_align(item, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER); // 移除
    
    // Left Container (safe inset + reserved right column)
    lv_obj_t * left_cont = lv_obj_create(item);
    lv_obj_clear_flag(left_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(left_cont, lv_pct(54));
    lv_obj_set_height(left_cont, lv_pct(100));
    lv_obj_align(left_cont, LV_ALIGN_LEFT_MID, 24, 0);
    lv_obj_set_style_bg_opa(left_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_cont, 0, 0);
    lv_obj_set_style_pad_all(left_cont, 0, 0);
    lv_obj_set_flex_flow(left_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(left_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(left_cont, LV_OBJ_FLAG_CLICKABLE); // Pass clicks to parent

    // Left: WiFi Name (Inside Left Container)
    lv_obj_t * lbl_name = lv_label_create(left_cont);
    lv_label_set_text(lbl_name, name);
    lv_obj_set_style_text_color(lbl_name, mimi_ui_color(MIMI_TEXT), 0);
    lv_obj_set_style_text_font(lbl_name, WIFI_FONT_BODY, 0);
    lv_obj_set_width(lbl_name, lv_pct(100)); // Fill left container
    lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_DOT); // 名字太长则省略

    // Right Container (safe inset)
    lv_obj_t * right = lv_obj_create(item);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(right, lv_pct(36));
    lv_obj_set_height(right, lv_pct(100));
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, -24, 0);
    lv_obj_set_style_bg_opa(right, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right, 0, 0);
    lv_obj_set_style_pad_all(right, 0, 0);
    // 右侧容器内部依然可以用 Flex，只要它不超出预留边界
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_gap(right, 10, 0);
    lv_obj_clear_flag(right, LV_OBJ_FLAG_CLICKABLE); // Pass clicks to parent

    // 调整顺序：先创建 Status Label，再创建 Lock Icon
    // 这样 Lock Icon 会显示在最右侧，避免被 Status 文字挤压或覆盖
    lv_obj_t * lbl_status = lv_label_create(right);
    lv_obj_set_style_text_color(lbl_status, mimi_ui_color(MIMI_TEAL), 0);
    lv_obj_set_style_text_font(lbl_status, WIFI_FONT_BODY, 0);
    lv_obj_set_width(lbl_status, lv_pct(76));
    lv_label_set_long_mode(lbl_status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(lbl_status, LV_TEXT_ALIGN_RIGHT, 0);
    // 不再需要 min_width，因为容器宽度固定且隔离
    
    if (status && strlen(status) > 0) {
        lv_label_set_text(lbl_status, status);
        // 如果已连接，隐藏锁图标（即使它是加密的）
        /* 
         * FIX: 即使是已连接，我们也要创建锁图标（如果 locked=true），然后隐藏它。
         * 这样后续断开时，我们才能 unhide 它。
         * 之前的逻辑是：if (已连接) locked = false; -> 导致根本没创建 image 对象。
         */
    } else {
        lv_label_set_text(lbl_status, "");
    }

    if (locked) {
        lv_obj_t * lock = lv_image_create(right);
        lv_image_set_src(lock, &lock_outline_28dp_666666);
        lv_obj_set_size(lock, 28, 28);
        lv_image_set_inner_align(lock, LV_IMAGE_ALIGN_CONTAIN);
        
        // 如果当前是已连接状态，隐藏锁图标
        if (status && strcmp(status, WIFI_STATUS_CONNECTED) == 0) {
            lv_obj_add_flag(lock, LV_OBJ_FLAG_HIDDEN);
        }
    }

    return item;
}



void wifi_app_init(void) {
    printf("WiFi App Init\n");
    wifi_app_active = true;
    wifi_screen = NULL;
    wifi_list = NULL;
    spinner = NULL;
    scan_info_cont = NULL;
    sw_wlan = NULL;
    wlan_pending_label = NULL;
    password_kb = NULL;
    password_modal = NULL;
    password_ta = NULL;
    password_error_label = NULL;
    is_scanning = false;
    wlan_toggle_pending = false;
    runtime_status_received = false;
    startup_scan_requested = false;
    is_wifi_enabled = sys_settings_get()->wifi_enabled;

    lv_obj_t *scr = lv_obj_create(NULL);
    wifi_screen = scr;
    mimi_ui_header(scr, ui_tr(UI_STR_APP_WIFI), back_event_handler);
    lv_obj_t *body = mimi_ui_body(scr);
    lv_obj_update_layout(body);
    int32_t width = lv_obj_get_width(body);
    int32_t height = lv_obj_get_height(body);
    lv_obj_t *intro = lv_obj_create(body);
    mimi_ui_surface(intro, 16);
    lv_obj_set_size(intro, 278, height);
    lv_obj_set_style_pad_all(intro, 22, 0);
    lv_obj_t *heading = mimi_ui_text(intro,
        ui_i18n_is_zh() ? "保持连接" : "Stay connected", ui_font_h2(), MIMI_TEXT);
    lv_obj_set_width(heading, lv_pct(100));
    lv_obj_t *hint = mimi_ui_text(intro, ui_i18n_is_zh() ?
        "设备会优先连接已保存的网络。" : "Mimi prefers saved networks automatically.",
        ui_font_body_md(), MIMI_MUTED);
    lv_obj_set_pos(hint, 0, 72);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_t *symbol = mimi_ui_text(intro, LV_SYMBOL_WIFI, &lv_font_montserrat_48, MIMI_TEAL);
    lv_obj_center(symbol);
    lv_obj_t *cont = lv_obj_create(body);
    mimi_ui_surface(cont, 16);
    lv_obj_set_pos(cont, 296, 0);
    lv_obj_set_size(cont, width - 296, height);
    lv_obj_set_style_pad_all(cont, 18, 0);
    lv_obj_set_style_pad_row(cont, 16, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

    /* WLAN Switch Container */
    lv_obj_t * wlan_cont = lv_obj_create(intro);
    lv_obj_set_width(wlan_cont, lv_pct(100));
    lv_obj_set_height(wlan_cont, 110);
    lv_obj_align(wlan_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(wlan_cont, mimi_ui_color(MIMI_SURFACE), 0);
    lv_obj_set_style_radius(wlan_cont, 16, 0);
    lv_obj_set_style_pad_all(wlan_cont, 0, 0);
    lv_obj_set_layout(wlan_cont, LV_LAYOUT_NONE);
    lv_obj_clear_flag(wlan_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(wlan_cont, 0, 0);
    
    lv_obj_t * lbl_wlan = lv_label_create(wlan_cont);
    lv_label_set_text(lbl_wlan, ui_tr(UI_STR_APP_WIFI));
    lv_obj_align(lbl_wlan, LV_ALIGN_TOP_LEFT, 0, 12);
    lv_obj_set_style_text_font(lbl_wlan, WIFI_FONT_SECTION, 0);
    lv_obj_set_style_text_color(lbl_wlan, mimi_ui_color(MIMI_TEXT), 0);
    wlan_pending_label = lv_label_create(wlan_cont);
    lv_label_set_text(wlan_pending_label, "");
    lv_obj_align(wlan_pending_label, LV_ALIGN_BOTTOM_LEFT, 0, -12);
    lv_obj_set_width(wlan_pending_label, lv_pct(100));
    lv_obj_set_style_text_font(wlan_pending_label, WIFI_FONT_META, 0);
    lv_obj_set_style_text_color(wlan_pending_label, mimi_ui_color(MIMI_MUTED), 0);
    
    sw_wlan = lv_switch_create(wlan_cont);
    lv_obj_set_size(sw_wlan, 52, 30);
    lv_obj_align(sw_wlan, LV_ALIGN_TOP_RIGHT, 0, 12);
    lv_obj_set_ext_click_area(sw_wlan, 9);
    lv_obj_set_style_bg_color(sw_wlan, mimi_ui_color(MIMI_TEAL), LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (is_wifi_enabled) lv_obj_add_state(sw_wlan, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw_wlan, wlan_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Info Container: Label + Refresh Button */
    scan_info_cont = lv_obj_create(cont);
    lv_obj_set_width(scan_info_cont, lv_pct(100));
    lv_obj_set_height(scan_info_cont, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(scan_info_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scan_info_cont, 0, 0);
    lv_obj_set_style_pad_all(scan_info_cont, 0, 0);
    lv_obj_set_flex_flow(scan_info_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(scan_info_cont, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    
    if (!is_wifi_enabled) lv_obj_add_flag(scan_info_cont, LV_OBJ_FLAG_HIDDEN);

    /* Separator Label */
    lv_obj_t * lbl_sep = lv_label_create(scan_info_cont);
    lv_label_set_text(lbl_sep, ui_tr(UI_STR_AVAILABLE_NETWORKS));
    lv_obj_set_style_text_font(lbl_sep, WIFI_FONT_META, 0);
    lv_obj_set_style_text_color(lbl_sep, mimi_ui_color(MIMI_MUTED), 0); // Light Gray
    lv_obj_set_style_pad_left(lbl_sep, 20, 0); // Align with card padding

    /* Refresh Button - Moved inside container */
    lv_obj_t * btn_refresh = lv_button_create(scan_info_cont);
    lv_obj_set_size(btn_refresh, 100, 50); // Wider for text
    lv_obj_set_style_bg_opa(btn_refresh, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(btn_refresh, 0, 0);
    // Removed absolute positioning
    lv_obj_add_event_cb(btn_refresh, scan_click_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t * lbl_refresh = lv_label_create(btn_refresh);
    lv_label_set_text(lbl_refresh, ui_tr(UI_STR_REFRESH));
    lv_obj_set_style_text_font(lbl_refresh, WIFI_FONT_META, 0);
    lv_obj_set_style_text_color(lbl_refresh, mimi_ui_color(MIMI_TEAL), 0); // Theme Primary Color
    lv_obj_center(lbl_refresh);

    /* === Content (List) === */
    wifi_list = lv_obj_create(cont);
    lv_obj_set_width(wifi_list, lv_pct(100));
    lv_obj_set_flex_grow(wifi_list, 1); // Fill remaining space
    lv_obj_set_style_bg_opa(wifi_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wifi_list, 0, 0);
    lv_obj_set_style_pad_all(wifi_list, 0, 0);
    // lv_obj_set_style_pad_right(wifi_list, 15, 0); // Removed padding to align with WLAN container
    lv_obj_set_scrollbar_mode(wifi_list, LV_SCROLLBAR_MODE_AUTO); // Indicate additional networks
    lv_obj_set_flex_flow(wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wifi_list, 0, 0); // 20px vertical gap

    // Initial Scan
    // scan_click_cb(NULL); // Call manually or wait for user
    
    lv_scr_load(scr);
    
    // Register IPC Listener after UI widgets are ready
    mw_subscribe(TOPIC_WIFI_STATUS, wifi_status_handler);
    mw_subscribe(TOPIC_WIFI_RUNTIME, wifi_runtime_handler);

    /* Do not trust only the persisted toggle: Wi-Fi may have recovered after
     * a deferred driver probe while this app was closed. */
    wifi_cmd_t runtime_cmd = { .action = CMD_GET_RUNTIME };
    mw_publish(TOPIC_WIFI_COMMAND, &runtime_cmd, sizeof(runtime_cmd),
               MW_DIR_UI_TO_BACKEND);

    // Auto-scan on open if enabled
    if (is_wifi_enabled) {
        scan_click_cb(NULL);
    }
}

void wifi_app_close(void) {
    printf("WiFi App Closed\n");
    wifi_app_active = false;
    is_scanning = false;
    wifi_spinner_delete();
    if (password_modal) {
        lv_obj_delete(password_modal);
        password_modal = NULL;
        password_kb = NULL;
        password_ta = NULL;
    }
    if (wifi_settings_dirty) {
        sys_settings_save();
        wifi_settings_dirty = false;
    }
    mw_unsubscribe(TOPIC_WIFI_STATUS, wifi_status_handler);
    mw_unsubscribe(TOPIC_WIFI_RUNTIME, wifi_runtime_handler);
    wifi_screen = NULL;
    wifi_list = NULL;
    scan_info_cont = NULL;
    sw_wlan = NULL;
    wlan_pending_label = NULL;
    wlan_toggle_pending = false;
    runtime_status_received = false;
    startup_scan_requested = false;
    password_kb = NULL;
    password_modal = NULL;
    password_ta = NULL;
    password_error_label = NULL;
}

AppDescriptor app_wifi = {
    .id = APP_ID_WIFI,
    .name = "WiFi",
    .init = wifi_app_init,
    .close = wifi_app_close
};
