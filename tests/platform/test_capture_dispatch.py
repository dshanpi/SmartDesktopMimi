"""Behavioral priority check using the exact selected SDK dispatch and queue."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from tests.sdk_source import capture_source

class CaptureDispatchTests(unittest.TestCase):
    def test_secondary_work_does_not_delay_current_encoder_publication(self):
        source = capture_source("main.cpp").read_text()
        header = capture_source("latest_frame.h")
        start = source.index("buffer_returned = true;", source.index("static void* capture_thread"))
        start = source.index(";", start) + 1
        end = source.index("if ((frame_count % 300)", start)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "dispatch.inc").write_text(source[start:end])
            (root / "latest_frame.h").write_bytes(header.read_bytes())
            fixture = Path(__file__).with_name("fixtures") / "capture_dispatch.cpp"
            command = ["g++", "-std=c++11", "-pthread", "-Wall", "-Wextra", "-Werror",
                       "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie",
                       '-DDISPATCH="dispatch.inc"', "-I" + str(root), str(fixture), "-o", str(root / "test")]
            subprocess.run(command, check=True, capture_output=True, timeout=30)
            for stage, enabled in (("1", True), ("2", True), ("1", False), ("2", False)):
                with self.subTest(held_secondary=stage, stream_enabled=enabled):
                    args = [str(root / "test"), stage] + ([] if enabled else ["disabled"])
                    result = subprocess.run(args, capture_output=True, text=True, timeout=10)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn("frame_valid=" + str(int(enabled)), result.stdout)

if __name__ == "__main__":
    unittest.main()
