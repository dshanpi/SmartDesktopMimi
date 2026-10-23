"""Compile exact production stop routines with fault/queue fixtures.
Host sanitizer results do not establish physical capture shutdown.
"""
import pathlib
import subprocess
import sys
import tempfile
import unittest

FIXTURES = pathlib.Path(__file__).resolve().parent / "fixtures"

class TuyaStopTests(unittest.TestCase):
    def test_command_dispatch(self):
        self.run_fixture("command")

    def test_audio_cleanup_errors(self):
        self.run_fixture("audio")

    def test_input_request_completion(self):
        self.run_fixture("input")

    def test_worker_fifo(self):
        self.run_fixture("worker")

    def test_exit_and_enable_errors(self):
        self.run_fixture("exit")

    def run_fixture(self, name):
        with tempfile.TemporaryDirectory() as tmp:
            subprocess.run([sys.executable, str(FIXTURES / ("tuya_stop_" + name + ".py")), tmp],
                           check=True, timeout=30)

if __name__ == "__main__":
    unittest.main()
