"""Run exact selected provider functions with fault-injected storage/hardware stubs.
Set MIMI_PROVIDER_RUNTIME_SOURCE to the versioned service_ai_runtime.c under test.
This checks selected control flow, not device or full translation-unit behavior.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

class BackendSettingsTests(unittest.TestCase):
    def test_reset_retry_offline_shutdown(self):
        value=os.environ.get('MIMI_PROVIDER_RUNTIME_SOURCE')
        if not value:self.skipTest('selected SDK source required')
        source=Path(value).read_text();fixtures=Path(__file__).parent/'fixtures'
        def extract(signature):
            start=source.index(signature);i=source.index('{',start)+1;depth=1
            while depth:
                depth+=(source[i]=='{')-(source[i]=='}');i+=1
            return source[start:i]
        functions=[extract(name) for name in ['static bool save_tuya_iot_preferences(void)',
            'static void reset_tuya_iot_preferences(void)','void service_ai_deinit(void)','void service_ai_update(void)']]
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);c=root/'test.c';binary=root/'test'
            c.write_text((fixtures/'backend_settings_head.c').read_text()+'\n'+'\n\n'.join(functions)+'\n'+(fixtures/'backend_settings_tail.c').read_text())
            subprocess.run(['gcc','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(c),'-o',str(binary)],check=True)
            for case in ['reset','periodic','offline','shutdown']:
                with self.subTest(case=case):subprocess.run([str(binary),case],check=True)
if __name__=='__main__':unittest.main()
