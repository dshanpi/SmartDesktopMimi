import os
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]

class PlatformInitTests(unittest.TestCase):
    def invoke(self, name, action, failed):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);log=root/'calls';script=root/'init'
            original=ROOT/('packaging/aitvbox-platform/files/aitvbox-'+name+'.init')
            script.write_text(original.read_text().replace('/usr/bin/aitvbox-agentctl','agentctl'))
            wrapper = r'''mkdir(){ echo mkdir >> "$LOG"; [ "$FAIL" != mkdir ]; }
chmod(){ echo chmod >> "$LOG"; [ "$FAIL" != chmod ]; }
procd_open_instance(){ echo open >> "$LOG"; }
procd_set_param(){ echo param >> "$LOG"; }
procd_close_instance(){ echo close >> "$LOG"; }
agentctl(){ echo agentctl >> "$LOG"; [ "$FAIL" = none ]; }
logger(){ echo "logger $*" >> "$LOG"; [ "$FAIL" != logger ]; }
rm(){ echo cleanup >> "$LOG"; }
. "$SCRIPT"
"$ACTION"
'''
            result=subprocess.run(['sh','-c',wrapper],env=dict(os.environ,LOG=str(log),FAIL=failed,SCRIPT=str(script),ACTION=action),capture_output=True,text=True)
            return result,log.read_text().splitlines()

    def test_failed_setup_never_registers_service(self):
        for name in ('apps','control'):
            for failed in ('mkdir','chmod','none'):
                with self.subTest(name=name,failed=failed):
                    result,calls=self.invoke(name,'start_service',failed)
                    if failed=='none':
                        self.assertEqual(result.returncode,0);self.assertIn('open',calls);self.assertIn('close',calls)
                    else:
                        self.assertNotEqual(result.returncode,0)
                        self.assertNotIn('open',calls)

    def test_stop_failure_observable_without_skipping_cleanup(self):
        for failed in ('agentctl','logger','none'):
            with self.subTest(failed=failed):
                result,calls=self.invoke('control','stop_service',failed)
                self.assertIn('agentctl',calls);self.assertEqual(calls[-1],'cleanup')
                logged=[line for line in calls if line.startswith('logger ')]
                if failed=='none': self.assertFalse(logged)
                else:
                    self.assertTrue(logged)
                    if failed=='logger': self.assertIn('stop request or HID release failed',result.stderr)
