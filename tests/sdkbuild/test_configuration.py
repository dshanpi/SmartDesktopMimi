from pathlib import Path
import importlib.util,json,subprocess,tempfile,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('configure',ROOT/'tools/sdkbuild/configure.py');c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
class ConfigurationTests(unittest.TestCase):
 def profile(self):
  return dict(schema_version=1,platform_id='a133',target_os='tina',package_format='mimi-application',architecture='aarch64',sdk_lock_sha256='a'*64,system_baseline='qualified-fixture',screens=[dict(width=1024,height=768)],voice_control=False)
 def test_screen_inputs_reject_duplicates_injection_and_bad_bounds(self):
  for screens in [[],['1024x768','1024x768'],['1024x768;reboot'],['0x768'],['479x800'],['10000x768']]:
   with self.subTest(screens=screens),self.assertRaises(ValueError):c.resolutions(screens)
  self.assertEqual(c.resolutions(['480×800','1920x1080'])[0],dict(width=480,height=800))
 def test_unknown_platform_os_and_format_do_not_fall_back(self):
  for key,value in [('platform_id','t527'),('platform_id','rk3576'),('platform_id','a527'),('target_os','unknown'),('package_format','deb'),('architecture','arm'),('voice_control','false')]:
   p=self.profile();p[key]=value
   with self.subTest(key=key,value=value),self.assertRaises(ValueError):c.validate(p)
 def test_unknown_profile_fields_and_boolean_geometry_rejected(self):
  p=self.profile();p['force']=True
  with self.assertRaises(ValueError):c.validate(p)
  p=self.profile();p['screens'][0]['width']=True
  with self.assertRaises(ValueError):c.validate(p)
 def test_tina_sdk_cannot_be_relabelled_debian(self):
  p=self.profile();p.update(target_os='debian',package_format='deb')
  with patch.object(c,'verify_sdk',return_value={k:p[k] for k in ['platform_id','architecture','system_baseline']}):
   with self.assertRaisesRegex(ValueError,'target OS'):c.check_sdk(p,Path('/missing'),False)
 def test_wrong_baseline_rejected_before_compiler_runs(self):
  p=self.profile();record={k:p[k] for k in ['platform_id','architecture','system_baseline']};record['system_baseline']='other'
  with patch.object(c,'verify_sdk',return_value=record),self.assertRaisesRegex(ValueError,'system_baseline'):c.check_sdk(p,Path('/missing'),False)
 def test_compiled_display_selection_is_exact(self):
  with tempfile.TemporaryDirectory() as d:
   d=Path(d);(d/'test.c').write_text('#include <assert.h>\n#include "display_profile.h"\nint main(){assert(mimi_display_profile_allows(1024,768));assert(mimi_display_profile_allows(480,800));assert(!mimi_display_profile_allows(800,480));assert(!mimi_display_profile_allows(1024,769));assert(!mimi_display_profile_allows(-1,768));}\n')
   subprocess.run(['cc','-DMIMI_DISPLAY_PROFILES="1024x768,480x800"','-I'+str(ROOT/'apps/lv_port_linux/src/ui'),str(d/'test.c'),str(ROOT/'apps/lv_port_linux/src/ui/display_profile.c'),'-o',str(d/'test')],check=True)
   subprocess.run([str(d/'test')],check=True)
if __name__=='__main__':unittest.main()
