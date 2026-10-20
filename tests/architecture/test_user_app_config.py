"""Compile the actual platform config reader; no services or device mutation."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT/'apps/lv_port_linux/src/system/config'
CLIENT = ROOT/'apps/lv_port_linux/src/apps/user'


class UserAppConfigTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='mimi-app-config-')
        src = Path(cls.build.name)/'main.c'
        src.write_text('#include "app_config.h"\n#include "user_app_config.h"\n#include <stdio.h>\nint main(void){const mimi_user_app_config_t *c=mimi_user_app_config_get();printf("%s\\n%s\\n%s\\n",c->user_apps_root,c->user_apps_socket_path,app_config_get()->ipc_socket_path);return 0;}\n')
        cls.binary = Path(cls.build.name)/'config'
        subprocess.run(['cc','-Wall','-Wextra','-Werror','-I'+str(CONFIG),'-I'+str(CLIENT),str(src),str(CONFIG/'app_config.c'),str(CLIENT/'user_app_config.c'),'-o',str(cls.binary)],check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def read(self, content):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'platform.conf';p.write_text(content)
            r=subprocess.run([str(self.binary)],env=dict(os.environ,LV_PORT_LINUX_CONFIG=str(p)),capture_output=True,text=True,check=True)
            return r.stdout.splitlines()

    def test_missing_explicit_config_does_not_fall_back(self):
        with tempfile.TemporaryDirectory() as tmp:
            r=subprocess.run([str(self.binary)],env=dict(os.environ,LV_PORT_LINUX_CONFIG=str(Path(tmp)/'missing')),capture_output=True,text=True,check=True)
            self.assertEqual(r.stdout.splitlines()[:2],['',''])

    def test_legacy_defaults(self):
        self.assertEqual(self.read('')[:2],['/overlay/aitvbox/apps','/var/run/aitvbox/apps.sock'])

    def test_independent_platform_endpoints(self):
        self.assertEqual(self.read('user_apps.root=/var/lib/mimi/apps\nuser_apps.socket_path=/run/mimi/apps.sock\nipc.socket_path=/run/mimi/ipc.sock\n'),['/var/lib/mimi/apps','/run/mimi/apps.sock','/run/mimi/ipc.sock'])

    def test_blank_disables_without_fallback(self):
        self.assertEqual(self.read('user_apps.root=\nuser_apps.socket_path=\n')[:2],['',''])

    def test_each_endpoint_is_independent(self):
        self.assertEqual(self.read('user_apps.root=/var/lib/mimi/apps\nuser_apps.socket_path=\n')[:2],['/var/lib/mimi/apps',''])

    def test_unsafe_paths_disable(self):
        for path in ['relative/apps','/','/tmp/../apps','/tmp/./apps','/tmp//apps','/tmp/apps/','/tmp/ap\tps','/tmp/ap\x7fps']:
            with self.subTest(path=path):
                self.assertEqual(self.read('user_apps.root='+path+'\nuser_apps.socket_path='+path+'\n')[:2],['',''])

    def test_oversize_never_truncated(self):
        self.assertEqual(self.read('user_apps.root=/'+'x'*255+'\nuser_apps.socket_path=/'+'y'*107+'\n')[:2],['',''])

    def test_boundary_and_internal_spaces(self):
        root='/'+'x'*254;socket='/'+'s'*106
        self.assertEqual(self.read('user_apps.root='+root+'\nuser_apps.socket_path='+socket+'\n')[:2],[root,socket])
        self.assertEqual(self.read('user_apps.root=/var/lib/my apps\n')[0],'/var/lib/my apps')

    def test_nul_or_overlong_config_line_disables_app_endpoints(self):
        for content in ['user_apps.root=/valid/apps\0hidden\n',
                        'user_apps.root=/valid/apps'+' '*600+'junk\n',
                        'user_apps.root=/valid/apps\n'+'x'*600+'\n']:
            with self.subTest(content=content):
                self.assertEqual(self.read(content)[:2],['',''])

    def test_invalid_override_does_not_retain_previous_endpoint(self):
        self.assertEqual(self.read('user_apps.root=/valid/apps\nuser_apps.root=../bad\n')[0],'')


if __name__=='__main__':unittest.main()
