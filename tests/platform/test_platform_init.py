import os
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]

class PlatformInitTests(unittest.TestCase):
    def invoke(self, name, action, failed, outer=False):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);log=root/'calls';script=root/'init'
            original=ROOT/('packaging/aitvbox-platform/files/aitvbox-'+name+'.init')
            script.write_text(original.read_text().replace('/usr/bin/aitvbox-agentctl','agentctl'))
            wrapper = r'''mkdir(){ echo mkdir >> "$LOG"; [ "$FAIL" != mkdir ]; }
chmod(){ echo chmod >> "$LOG"; [ "$FAIL" != chmod ]; }
list_contains(){ return 0; }
procd_open_service(){ echo service_open >> "$LOG"; }
procd_close_service(){ echo service_submit >> "$LOG"; }
procd_open_instance(){ echo open >> "$LOG"; }
procd_set_param(){ echo param >> "$LOG"; }
procd_close_instance(){ echo close >> "$LOG"; }
agentctl(){ echo agentctl >> "$LOG"; [ "$FAIL" = none ]; }
logger(){ echo "logger $*" >> "$LOG"; [ "$FAIL" != logger ]; }
rm(){ echo cleanup >> "$LOG"; }
. "$SCRIPT"
"$ACTION"
'''
            env=dict(os.environ,LOG=str(log),FAIL=failed,SCRIPT=str(script),ACTION=action)
            if outer:
                rc=Path(os.environ['MIMI_RC_COMMON']).resolve()
                import hashlib
                self.assertEqual(hashlib.sha256(rc.read_bytes()).hexdigest(), '54a393491745cbef8653076b00eddec6a20ff587c4d9b9d1e0814ba8d15f16f3')
                (root/'lib/functions').mkdir(parents=True)
                for library in ('functions.sh','functions/service.sh','functions/procd.sh'):
                    (root/'lib'/library).write_text('')
                env.update(IPKG_INSTROOT=str(root),RC_COMMON=str(rc),ACTION="start" if action=="start_service" else action)
                wrapper=wrapper.replace('. "$SCRIPT"\n"$ACTION"', 'set -- "$SCRIPT" "$ACTION"\n. "$RC_COMMON"')
            result=subprocess.run(['sh','-c',wrapper],env=env,capture_output=True,text=True)
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

    def test_stop_failure_observable_without_removing_socket(self):
        for failed in ('agentctl','logger','none'):
            with self.subTest(failed=failed):
                result,calls=self.invoke('control','stop_service',failed)
                self.assertIn('agentctl',calls);self.assertNotIn('cleanup',calls)
                logged=[line for line in calls if line.startswith('logger ')]
                if failed=='none': self.assertFalse(logged)
                else:
                    self.assertTrue(logged)
                    if failed=='logger': self.assertIn('stop request or HID release failed',result.stderr)

    def test_integrated_rc_common_startup(self):
        if not os.environ.get('MIMI_RC_COMMON'):
            self.skipTest('selected SDK rc.common required')
        for name in ('apps','control'):
            for failed in ('mkdir','chmod','none'):
                with self.subTest(name=name,failed=failed):
                    result,calls=self.invoke(name,'start_service',failed,outer=True)
                    if failed=='none':
                        self.assertEqual(result.returncode,0,result.stderr)
                        self.assertIn('open',calls);self.assertEqual(calls[-1],'service_submit')
                    else:
                        self.assertNotEqual(result.returncode,0)
                        self.assertNotIn('open',calls);self.assertNotIn('service_submit',calls)

    def test_stop_preserves_daemon_owned_socket(self):
        import socket
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            endpoint = root / 'control.sock'
            script = root / 'init'
            source = ROOT / 'packaging/aitvbox-platform/files/aitvbox-control.init'
            script.write_text(source.read_text().replace('/usr/bin/aitvbox-agentctl', 'true')
                              .replace('/var/run/aitvbox/control.sock', str(endpoint)))
            with socket.socket(socket.AF_UNIX) as server:
                server.bind(str(endpoint))
                inode = endpoint.stat().st_ino
                subprocess.run(['sh', '-c', '. "$1"; stop_service', 'sh', str(script)], check=True)
                self.assertTrue(endpoint.exists(), 'init deleted live daemon endpoint before process exit')
                self.assertEqual(endpoint.stat().st_ino, inode)
