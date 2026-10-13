"""Exercise actual product calibration logic against gt9xx ioctl behavior."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class EvdevCalibrationTests(unittest.TestCase):
    def test_valid_ranges_and_gt9xx_empty_legacy_axes(self):
        app = ROOT / 'apps/lv_port_linux'
        fixture = ROOT / 'tests/architecture/fixtures/evdev_calibration.c'
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'calibration'
            subprocess.run([
                'gcc', '-O1', '-ffunction-sections', '-fdata-sections',
                '-DLV_CONF_SKIP', '-DLV_USE_EVDEV=1',
                '-I' + str(app), '-I' + str(app / 'lvgl'), str(fixture),
                '-Wl,--gc-sections', '-o', str(binary),
            ], check=True, capture_output=True, text=True)
            result = subprocess.run([str(binary)], check=True, capture_output=True, text=True)
            self.assertIn('PASS:', result.stdout)


if __name__ == '__main__':
    unittest.main()
