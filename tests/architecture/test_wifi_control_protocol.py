"""Compile the optional WiFi byte contract and frozen V1 compatibility checks."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class WifiControlProtocolTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("gcc"), "gcc required")
    def test_wire_contract_and_legacy_ids(self):
        with tempfile.TemporaryDirectory(prefix="mimi-wifi-wire-") as folder:
            binary = pathlib.Path(folder) / "test"
            build = subprocess.run([
                "gcc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-I", str(ROOT / "apps/lv_port_linux/src"),
                str(ROOT / "tests/architecture/fixtures/wifi_control_protocol.c"),
                "-o", str(binary),
            ], capture_output=True, text=True, timeout=30)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("76 malformed messages", run.stdout)
            self.assertIn("144 malformed replies", run.stdout)
            self.assertEqual(run.stderr, "")


    @unittest.skipUnless(shutil.which("gcc"), "gcc required")
    def test_actual_ipc_transport(self):
        with tempfile.TemporaryDirectory(prefix="mimi-wifi-ipc-") as folder:
            binary = pathlib.Path(folder) / "test"
            build = subprocess.run([
                "gcc", "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
                "-I", str(ROOT / "apps/lv_port_linux/src"),
                str(ROOT / "tests/architecture/fixtures/wifi_control_transport.c"),
                str(ROOT / "apps/lv_port_linux/src/ipc/ipc_socket.c"),
                "-o", str(binary),
            ], capture_output=True, text=True, timeout=30)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("PASS actual IPC codec", run.stdout)
            self.assertEqual(run.stderr, "")


if __name__ == "__main__":
    unittest.main()
