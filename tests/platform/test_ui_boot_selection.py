import os, pathlib, socket, subprocess, tempfile, unittest
ROOT=pathlib.Path(__file__).resolve().parents[2]
class BootSelectionTests(unittest.TestCase):
 def test_failed_selection_preserves_boot_screen(self):
  self.check_failure(False)
 def test_missing_selected_program_preserves_boot_screen(self):
  self.check_failure(True)
 def check_failure(self, missing):
  with tempfile.TemporaryDirectory() as temp:
   r=pathlib.Path(temp);bin=r/'bin';bin.mkdir();progress=r/'progress';progress.write_text('84\n')
   for name,body in {'pidof':'[ "$1" = lv_backend ]','sleep':'exit 0','logger':'exit 0','mimi-update':'exit 7','lvglsim':'exit 0'}.items():
    p=bin/name;p.write_text('#!/bin/sh\n'+body+'\n');p.chmod(0o755)
   if missing:(bin/'mimi-update').write_text('#!/bin/sh\necho '+str(r/'missing-release')+'\n')
   config=r/'updater.json';config.touch();enrollment=r/'enrollment.json';enrollment.touch()
   source=(ROOT/'packaging/aitvbox-suite/files/start-ui.sh').read_text()
   for old,new in {'/usr/bin/lvglsim':str(bin/'lvglsim'),'/usr/bin/mimi-update':str(bin/'mimi-update'),'/etc/smartdesktopmimi/updater.json':str(config),'/overlay/smartdesktopmimi-platform/enrollment.json':str(enrollment),'/dev/fb0':'/dev/null','/dev/console':'/dev/null','/proc/bus/input/devices':'/dev/null','/tmp/lvglsim.log':str(r/'ui.log'),'/tmp/lv_backend.log':str(r/'backend.log')}.items():source=source.replace(old,new)
   script=r/'launcher';script.write_text(source)
   with socket.socket(socket.AF_UNIX) as backend:
    endpoint=r/'backend.sock';backend.bind(str(endpoint))
    result=subprocess.run(['sh',str(script)],env={**os.environ,'PATH':str(bin)+':'+os.environ['PATH'],'BOOT_PROGRESS_FILE':str(progress),'LV_BACKEND_SOCKET':str(endpoint),'MIMI_RELEASE_DIR':''},capture_output=True,timeout=5)
   self.assertNotEqual(result.returncode,0)
   self.assertEqual(progress.read_text(),'84\n','animation handover requested before release selection succeeded')
