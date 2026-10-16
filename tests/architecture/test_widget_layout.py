"""UI075 pure layout/store contract; actual pointer tests live in native_ui_review.c."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class WidgetLayoutTest(unittest.TestCase):
    def test_actual_settings_defaults_and_save_errors(self):
        with tempfile.TemporaryDirectory(prefix="mimi-ui075-settings-build-") as output:
            binary = Path(output) / "settings-test"
            command = ["cc", "-Wall", "-Wextra", "-Werror", "-g",
                       "-I" + str(ROOT / "apps/lv_port_linux/src/system"),
                       str(ROOT / "tests/architecture/fixtures/test_settings_ui075.c"),
                       str(ROOT / "apps/lv_port_linux/src/system/settings.c"),
                       "-o", str(binary)]
            if os.getenv("UI075_SANITIZE") == "1":
                command.insert(1, "-fsanitize=address,undefined")
            subprocess.run(command, check=True, capture_output=True, text=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("PASS UI075 actual settings", result.stdout)

    def test_real_store_and_draft_contract(self):
        with tempfile.TemporaryDirectory(prefix="mimi-ui075-build-") as output:
            binary = Path(output) / "layout-test"
            command = ["cc", "-Wall", "-Wextra", "-Werror", "-g",
                       "-I" + str(ROOT / "apps/lv_port_linux/src/ui"),
                       str(ROOT / "tests/architecture/fixtures/test_widget_layout.c"),
                       str(ROOT / "apps/lv_port_linux/src/ui/desktop_widget_layout.c"),
                       "-ljson-c", "-o", str(binary)]
            if os.getenv("UI075_SANITIZE") == "1":
                command.insert(1, "-fsanitize=address,undefined")
            subprocess.run(command, check=True, capture_output=True, text=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn("PASS UI075", result.stdout)
            print(result.stdout.strip())


if __name__ == "__main__":
    unittest.main()
