import pathlib, shutil, subprocess, tempfile, unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]
class WifiStatusDecodeTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("gcc"), "gcc required")
    def test_bounded_status(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = str(pathlib.Path(folder)/"test")
            subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-I", str(ROOT/"apps/lv_port_linux/src"), str(ROOT/"tests/architecture/fixtures/wifi_status_decode.c"), "-o", binary], check=True, capture_output=True, timeout=30)
            run = subprocess.run([binary], capture_output=True, text=True, timeout=15)
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
            self.assertEqual(run.stderr, "")
            self.assertIn("PASS 1270", run.stdout)
if __name__ == "__main__": unittest.main()
