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

 def test_selected_ui_handoff_exit_restore_and_stuck_animation(self):
  for status in (0,5,'stuck'):
   with self.subTest(status=status), tempfile.TemporaryDirectory() as temp:
    r=pathlib.Path(temp);bin=r/'bin';bin.mkdir();release=r/'release';(release/'bin').mkdir(parents=True)
    progress=r/'progress';progress.write_text('84\n');events=r/'events'
    def executable(path,body):
     path.write_text('#!/bin/sh\n'+body+'\n');path.chmod(0o755)
    executable(bin/'pidof','[ "$1" = lv_backend ] && exit 0\n[ "$1" = boot-play ] && [ "$STUCK" = 1 ]')
    executable(bin/'sleep','exit 0');executable(bin/'logger','exit 0')
    executable(bin/'killall','echo kill-request >>"$EVENTS"; exit 0')
    executable(bin/'boot-play','echo restored >>"$EVENTS"')
    executable(release/'bin/lvglsim','[ "$(cat "$BOOT_PROGRESS_FILE")" = 100 ] || exit 90\necho ui-start >>"$EVENTS"\nexit '+str(status if status!='stuck' else 0))
    source=(ROOT/'packaging/aitvbox-suite/files/start-ui.sh').read_text()
    for old,new in {'/usr/bin/lvglsim':str(bin/'missing-fallback'),'/dev/fb0':'/dev/null','/dev/console':'/dev/null','/proc/bus/input/devices':'/dev/null','/tmp/lvglsim.log':str(r/'ui.log'),'/tmp/lv_backend.log':str(r/'backend.log')}.items():source=source.replace(old,new)
    script=r/'launcher';script.write_text(source)
    with socket.socket(socket.AF_UNIX) as backend:
     endpoint=r/'backend.sock';backend.bind(str(endpoint))
     result=subprocess.run(['sh',str(script)],env={**os.environ,'PATH':str(bin)+':'+os.environ['PATH'],'BOOT_PROGRESS_FILE':str(progress),'LV_BACKEND_SOCKET':str(endpoint),'MIMI_RELEASE_DIR':str(release),'EVENTS':str(events),'STUCK':'1' if status=='stuck' else '0'},capture_output=True,timeout=5)
    self.assertNotEqual(result.returncode,0)
    if status=='stuck':
     self.assertNotIn('ui-start',events.read_text());self.assertEqual(events.read_text().count('kill-request'),2)
    else:
     # Restore is launched in background; wait for its marker without assuming scheduling.
     import time
     deadline=time.monotonic()+2
     while time.monotonic()<deadline and (not events.exists() or 'restored' not in events.read_text()):time.sleep(.01)
     self.assertEqual(events.read_text().splitlines(),['ui-start','restored'])
     self.assertEqual(progress.read_text(),'84\n')
     self.assertEqual(result.returncode,1 if status==0 else status)
