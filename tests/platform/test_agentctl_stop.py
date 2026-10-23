import os
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]

class AgentStopTests(unittest.TestCase):
    def test_release_results_are_not_hidden(self):
        for failed in ('key', 'mouse', 'both', 'none'):
            with self.subTest(failed=failed), tempfile.TemporaryDirectory() as directory:
                root = Path(directory);log = root/'hid.log';stop = root/'agent.stop'
                hid = root/'hid';hid.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$TEST_LOG"\n[ "$TEST_FAIL" != both ] && [ "$1" != "$TEST_FAIL" ]\n');hid.chmod(0o755)
                env = dict(os.environ,AITVBOX_STOP_FILE=str(stop),AITVBOX_HIDCTL=str(hid),TEST_LOG=str(log),TEST_FAIL=failed)
                result = subprocess.run([str(ROOT/'platform/runtime/aitvbox-agentctl'),'stop'],env=env,capture_output=True,text=True)
                self.assertTrue(stop.is_file())
                self.assertEqual(log.read_text().splitlines(),['key 0 0','mouse 0 0 0 0'])
                if failed == 'none':
                    self.assertEqual(result.returncode,0,result.stderr)
                    self.assertIn('stop_requested',result.stdout)
                else:
                    self.assertNotEqual(result.returncode,0,'failed release reported success')
                    self.assertNotIn('OK',result.stdout)
                    self.assertIn('release',result.stderr)
