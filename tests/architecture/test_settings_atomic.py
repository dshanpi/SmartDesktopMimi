"""Real settings file persistence; host process exit is not device power loss."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[2]
class SettingsAtomicTests(unittest.TestCase):
    def test_failures_and_concurrent_writers(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary=Path(tmp)/"settings-test"
            subprocess.run(["cc","-std=c11","-D_GNU_SOURCE","-Wall","-Wextra","-fsanitize=address,undefined","-fno-sanitize-recover=all",
                "-I"+str(ROOT/"apps/lv_port_linux/src/system"),str(ROOT/"apps/lv_port_linux/src/system/settings.c"),
                str(ROOT/"tests/architecture/fixtures/test_settings_atomic.c"),"-Wl,--wrap=pwrite","-Wl,--wrap=fsync","-Wl,--wrap=rename","-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=30)
