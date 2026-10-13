#!/usr/bin/env python3
"""Source contracts for the 1024x768 Mimi home desktop."""

from pathlib import Path
import re
import unittest


REPO = Path(__file__).resolve().parents[2]
UI = REPO / "apps/lv_port_linux/src/ui"


def macro_value(source: str, name: str) -> int:
    match = re.search(rf"^#define\s+{name}\s+(\d+)\s*$", source, re.MULTILINE)
    if not match:
        raise AssertionError(f"missing numeric macro {name}")
    return int(match.group(1))


class DesktopHomeContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tokens = (UI / "desktop_home_tokens.h").read_text(encoding="utf-8")
        cls.dashboard = (UI / "desktop_clock.c").read_text(encoding="utf-8")
        cls.dock = (UI / "desktop_dock.c").read_text(encoding="utf-8")
        cls.status_bar = (UI / "desktop_status_bar.c").read_text(encoding="utf-8")
        cls.all_apps = (UI / "desktop_all_apps.c").read_text(encoding="utf-8")
        cls.wifi = (
            REPO / "apps/lv_port_linux/src/apps/wifi/wifi_app.c"
        ).read_text(encoding="utf-8")
        cls.settings = (
            REPO / "apps/lv_port_linux/src/apps/setting/setting_app.c"
        ).read_text(encoding="utf-8")
        cls.ai = (
            REPO / "apps/lv_port_linux/src/apps/ai/ai_app.c"
        ).read_text(encoding="utf-8")
        cls.i18n = (UI / "ui_i18n.c").read_text(encoding="utf-8")

    def test_vertical_layout_exactly_fits_the_panel(self):
        screen_h = macro_value(self.tokens, "HOME_SCREEN_H")
        status_h = macro_value(self.tokens, "HOME_STATUS_H")
        dashboard_h = macro_value(self.tokens, "HOME_DASHBOARD_H")
        dock_h = macro_value(self.tokens, "HOME_DOCK_H")
        gap = macro_value(self.tokens, "HOME_GAP")
        dashboard_y = macro_value(self.tokens, "HOME_DASHBOARD_Y")
        dock_y = macro_value(self.tokens, "HOME_DOCK_Y")
        content_x = macro_value(self.tokens, "HOME_CONTENT_X")
        content_w = macro_value(self.tokens, "HOME_CONTENT_W")
        self.assertGreaterEqual(dashboard_y - status_h, gap)
        self.assertGreaterEqual(dock_y - (dashboard_y + dashboard_h), gap)
        self.assertLessEqual(dock_y + dock_h, screen_h)
        self.assertLessEqual(content_x + content_w, macro_value(self.tokens, "HOME_SCREEN_W"))

    def test_primary_cards_route_to_real_applications(self):
        self.assertIn("app_manager_open(APP_ID_AI)", self.dashboard)
        self.assertIn("app_manager_open(APP_ID_HDMI_MCP)", self.dashboard)
        self.assertIn("app_manager_open(APP_ID_WIFI)", self.dashboard)
        self.assertIn("desktop_update_show()", self.dashboard)
        self.assertNotIn("app_manager_open(APP_ID_OTA)", self.dashboard)
        self.assertIn("app_manager_open(APP_ID_SETTING)", self.dashboard)
        self.assertIn("ui_all_apps_show()", self.dashboard)

    def test_wifi_scan_is_named_and_resyncs_the_backend_runtime(self):
        self.assertIn("case APP_ID_WIFI:", self.all_apps)
        self.assertIn("ui_tr(UI_STR_HOME_SCAN_CONNECT)", self.all_apps)
        self.assertIn('[UI_STR_HOME_SCAN_CONNECT] = T("扫描并连接", "Scan & connect")', self.i18n)
        subscribe_at = self.wifi.index(
            "mw_subscribe(TOPIC_WIFI_RUNTIME, wifi_runtime_handler)"
        )
        runtime_request_at = self.wifi.index(
            "wifi_cmd_t runtime_cmd = { .action = CMD_GET_RUNTIME }"
        )
        startup_scan_at = self.wifi.index("if (is_wifi_enabled)", runtime_request_at)
        self.assertLess(subscribe_at, runtime_request_at)
        self.assertLess(runtime_request_at, startup_scan_at)
        self.assertIn(
            "first_runtime_status && runtime->enabled && !runtime->scanning",
            self.wifi,
        )

    def test_100ask_pairing_state_stays_visible_in_settings(self):
        self.assertIn("ui_tr(UI_STR_SETTINGS_100ASK_PAIRING)", self.settings)
        self.assertIn("ui_tr(UI_STR_SETTINGS_100ASK_UNAVAILABLE)", self.settings)
        self.assertIn('[UI_STR_SETTINGS_100ASK_PAIRING] = T("100ask.net 配对", "100ask.net Pairing")', self.i18n)
        self.assertIn("request_fresh_bind_token();", self.settings)

    def test_tuya_bound_state_explains_why_pairing_qr_is_hidden(self):
        self.assertIn("status->tuya_bound && !status->tuya_bind_qr_pending", self.ai)
        self.assertIn("UI_STR_AI_PAIRED_QR_HIDDEN", self.ai)
        self.assertIn("UI_STR_AI_UNBIND_HINT", self.ai)
        self.assertIn("UI_STR_AI_SCHEMA_LIMITED_FMT", self.ai)
        self.assertIn("Paired to Tuya · pairing QR is hidden.", self.i18n)

    def test_bottom_navigation_routes_are_not_placeholders(self):
        self.assertNotIn("NAV_ASSISTANT", self.dock)
        self.assertNotIn("NAV_HDMI", self.dock)
        self.assertIn("ui_all_apps_show()", self.dock)
        self.assertIn("LV_OBJ_FLAG_CLICK_FOCUSABLE", self.dock)

    def test_home_chrome_follows_theme_and_video_viewport_keeps_contrast(self):
        self.assertIn("HOME_DARK_TEXT", self.tokens)
        self.assertIn("HOME_DARK_TEXT_MUTED", self.tokens)
        self.assertIn("ui_font_h2(), HOME_TEXT", self.dashboard)
        self.assertIn("ui_font_h1(), HOME_TEXT", self.dashboard)
        self.assertIn("ui_font_h2(), HOME_DARK_TEXT", self.dashboard)
        self.assertIn("HOME_TEXT_MUTED", self.dock)
        self.assertIn(
            "lv_obj_set_style_text_color(brand, HOME_TEXT, 0)",
            self.status_bar,
        )
        self.assertIn("HOME_SURFACE_RAISED", self.status_bar)
        self.assertIn("HOME_SURFACE_RAISED", self.dock)

    def test_touch_targets_meet_embedded_minimum(self):
        action = re.search(
            r"lv_obj_set_size\(button,\s*width,\s*(\d+)\);", self.dashboard
        )
        self.assertIsNotNone(action)
        self.assertGreaterEqual(int(action.group(1)), 48)
        handle = re.search(r"lv_obj_set_size\(handle,\s*(\d+),\s*(\d+)\);", self.dock)
        self.assertIsNotNone(handle)
        self.assertGreaterEqual(int(handle.group(2)), 48)
        self.assertNotIn("create_quick_tile", self.dashboard)

    def test_home_runtime_pauses_when_screen_is_hidden(self):
        self.assertIn("LV_EVENT_SCREEN_UNLOADED", self.dashboard)
        self.assertIn("LV_EVENT_SCREEN_LOADED", self.dashboard)
        self.assertIn("lv_anim_delete(ai_orb_glow, orb_pulse_exec)", self.dashboard)
        self.assertIn("lv_timer_pause(clock_timer)", self.dashboard)
        self.assertIn("lv_timer_resume(clock_timer)", self.dashboard)
        self.assertIn("lv_timer_ready(clock_timer)", self.dashboard)

    def test_clock_hides_unset_rtc_and_updates_on_minute_boundaries(self):
        self.assertIn("CLOCK_MIN_VALID_YEAR 2024", self.dashboard)
        self.assertIn("ui_tr(UI_STR_TIME_NOT_SET)", self.dashboard)
        self.assertIn("ui_tr(UI_STR_WELCOME_MIMI)", self.dashboard)
        self.assertIn("60000U - elapsed", self.dashboard)
        self.assertIn("label_set_text_if_changed", self.dashboard)

    def test_all_apps_supports_remote_grid_navigation(self):
        self.assertIn("ALL_APPS_COLUMN_COUNT 4", self.all_apps)
        self.assertIn("lv_group_add_obj(group, all_apps_grid)", self.all_apps)
        self.assertIn("lv_group_remove_obj(all_apps_grid)", self.all_apps)
        self.assertIn("key == LV_KEY_LEFT", self.all_apps)
        self.assertIn("key == LV_KEY_RIGHT", self.all_apps)
        self.assertIn("key == LV_KEY_UP", self.all_apps)
        self.assertIn("key == LV_KEY_DOWN", self.all_apps)
        self.assertIn("key == LV_KEY_ENTER", self.all_apps)
        self.assertIn("lv_obj_scroll_to_view_recursive(card, LV_ANIM_ON)", self.all_apps)


if __name__ == "__main__":
    unittest.main(verbosity=2)
