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

    def test_vertical_layout_exactly_fits_the_panel(self):
        screen_h = macro_value(self.tokens, "HOME_SCREEN_H")
        status_h = macro_value(self.tokens, "HOME_STATUS_H")
        dashboard_h = macro_value(self.tokens, "HOME_DASHBOARD_H")
        dock_h = macro_value(self.tokens, "HOME_DOCK_H")
        gap = macro_value(self.tokens, "HOME_GAP")
        self.assertEqual(screen_h, status_h + dashboard_h + dock_h + gap * 4)

    def test_primary_cards_route_to_real_applications(self):
        self.assertIn("app_manager_open(APP_ID_AI)", self.dashboard)
        self.assertIn("app_manager_open(APP_ID_HDMI_MCP)", self.dashboard)
        self.assertIn("app_manager_open(APP_ID_WIFI)", self.dashboard)
        self.assertIn("app_manager_open(APP_ID_OTA)", self.dashboard)
        self.assertIn("app_manager_open(APP_ID_SETTING)", self.dashboard)
        self.assertIn("ui_all_apps_show()", self.dashboard)

    def test_bottom_navigation_routes_are_not_placeholders(self):
        self.assertIn("app_manager_open(APP_ID_AI)", self.dock)
        self.assertIn("app_manager_open(APP_ID_HDMI_MCP)", self.dock)
        self.assertIn("ui_all_apps_show()", self.dock)
        self.assertIn("LV_OBJ_FLAG_CLICK_FOCUSABLE", self.dock)

    def test_touch_targets_meet_embedded_minimum(self):
        action = re.search(
            r"lv_obj_set_size\(button,\s*(\d+),\s*(\d+)\);", self.dashboard
        )
        tile = re.search(
            r"lv_obj_set_size\(tile,\s*(\d+),\s*(\d+)\);", self.dashboard
        )
        nav = re.search(
            r"lv_obj_set_size\(item,\s*(\d+),\s*(\d+)\);", self.dock
        )
        self.assertIsNotNone(action)
        self.assertIsNotNone(tile)
        self.assertIsNotNone(nav)
        for target in (action, tile, nav):
            self.assertGreaterEqual(int(target.group(2)), 48)


if __name__ == "__main__":
    unittest.main(verbosity=2)
