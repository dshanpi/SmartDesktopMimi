import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/render_ui_review.py"

class SceneSelectionTests(unittest.TestCase):
    def test_cli_rejects_unknown_before_build_or_output(self):
        with tempfile.TemporaryDirectory() as folder:
            output = pathlib.Path(folder)/"output"
            for name in ["password,wifi-connect-open", "wifi-typo", "bt-typo", "ai-typo", "iot-schema-nonsense", "iot-schema-7", "../home"]:
                run = subprocess.run([sys.executable, str(SCRIPT), "--pages", name, "--config", str(pathlib.Path(folder)/"missing"), "--output", str(output)], capture_output=True, text=True, timeout=10)
                self.assertEqual(run.returncode, 2, run.stdout+run.stderr)
                self.assertIn("invalid choice", run.stderr)
                self.assertFalse(output.exists())

    def test_registry_contains_exact_supported_variants(self):
        spec = importlib.util.spec_from_file_location("review_scenes", SCRIPT)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        names = module.supported_scenes()
        self.assertEqual(len(names), len(set(names)))
        for name in ["home", "password", "wifi-connect-lifecycle", "bt-connected", "ai-free-chat", "iot-schema-0", "iot-schema-6"]:
            self.assertIn(name, names)
        self.assertNotIn("iot-schema-7", names)

if __name__ == "__main__": unittest.main()
