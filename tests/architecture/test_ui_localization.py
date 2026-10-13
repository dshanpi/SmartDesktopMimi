#!/usr/bin/env python3
"""Contracts for system i18n, appearance settings and localized user apps."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]
UI = REPO / "apps/lv_port_linux/src/ui"
SYSTEM = REPO / "apps/lv_port_linux/src/system"


def load_aitapp_module():
    path = REPO / "tools/aitapp/aitapp.py"
    spec = importlib.util.spec_from_file_location("aitapp_localization_test", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class UiLocalizationContractTests(unittest.TestCase):
    def test_every_catalog_id_has_a_bilingual_translation(self):
        header = (UI / "ui_i18n.h").read_text(encoding="utf-8")
        source = (UI / "ui_i18n.c").read_text(encoding="utf-8")
        enum_body = header.split("typedef enum {", 2)[2].split("} ui_string_id_t", 1)[0]
        ids = []
        for line in enum_body.splitlines():
            token = line.strip().rstrip(",").split(" = ", 1)[0]
            if token.startswith("UI_STR_") and token != "UI_STR_COUNT":
                ids.append(token)
        self.assertGreater(len(ids), 100)
        for string_id in ids:
            self.assertIn("[{}] = T(".format(string_id), source)

    def test_settings_v7_migrates_to_auto_language_and_appearance(self):
        source = (SYSTEM / "settings.c").read_text(encoding="utf-8")
        self.assertIn("#define SETTINGS_VERSION 8", source)
        self.assertIn("sizeof(settings_v7_t)", source)
        self.assertIn("current_settings.locale_mode = SYS_LOCALE_AUTO", source)
        self.assertIn("current_settings.appearance_mode = SYS_APPEARANCE_AUTO", source)
        self.assertIn("MERGE_CHANGED_FIELD(locale_mode)", source)
        self.assertIn("MERGE_CHANGED_FIELD(appearance_mode)", source)

    def test_theme_and_language_changes_rebuild_the_home_screen(self):
        settings_ui = (
            REPO / "apps/lv_port_linux/src/apps/setting/setting_app.c"
        ).read_text(encoding="utf-8")
        theme = (UI / "theme/theme.c").read_text(encoding="utf-8")
        self.assertIn("ui_i18n_set_mode(mode)", settings_ui)
        self.assertIn("ui_theme_set_appearance(mode)", settings_ui)
        self.assertGreaterEqual(settings_ui.count("ui_rebuild_home();"), 2)
        self.assertIn("SYS_APPEARANCE_LIGHT", theme)
        self.assertIn("SYS_APPEARANCE_DARK", theme)

    def test_embedded_auto_locale_defaults_to_chinese(self):
        source = (UI / "ui_i18n.c").read_text(encoding="utf-8")
        self.assertIn("C/POSIX is not a user language choice", source)
        self.assertIn("The shipping device is Chinese-first", source)
        self.assertIn("English remains explicitly", source)


class AitappLocalizationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.aitapp = load_aitapp_module()

    def create_app(self, root):
        manifest = {
            "schema": 1,
            "id": "com.example.localized",
            "name": "Localized App",
            "version": "1.0.0",
            "publisher": "example",
            "ui": {"entry": "ui/main.json", "presentation": "embedded"},
            "permissions": [],
            "localizations": {
                "zh-CN": {"name": "本地化应用", "uiEntry": "ui/main.zh-CN.json"},
                "en-US": {"name": "Localized App", "uiEntry": "ui/main.en-US.json"},
            },
        }
        page = {
            "schema": 1,
            "title": "Localized App",
            "layout": {"type": "column", "children": []},
        }
        (root / "ui").mkdir()
        (root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
        for name in ("main.json", "main.zh-CN.json", "main.en-US.json"):
            (root / "ui" / name).write_text(json.dumps(page), encoding="utf-8")
        return manifest

    def test_localized_entries_are_accepted_and_validated(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            expected = self.create_app(root)
            loaded = self.aitapp.load_manifest(root)
            self.assertEqual(expected["localizations"], loaded["localizations"])

    def test_localized_entry_cannot_escape_the_ui_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            manifest = self.create_app(root)
            manifest["localizations"]["zh-CN"]["uiEntry"] = "ui/../secret.json"
            (root / "manifest.json").write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(SystemExit, "invalid localized UI entry"):
                self.aitapp.load_manifest(root)


if __name__ == "__main__":
    unittest.main(verbosity=2)
