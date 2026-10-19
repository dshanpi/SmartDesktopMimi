"""Missing logind display: only exact enrolled GNOME session metadata qualifies."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('login', ROOT/'services/mimi-ubuntu-provider/login.py')
login=importlib.util.module_from_spec(spec); spec.loader.exec_module(login)

class DisplayTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory(); self.addCleanup(self.tmp.cleanup)
  self.root=Path(self.tmp.name); self.uid=os.getuid()
  self.fixture('42')
 def fixture(self,pid,display=':7'):
  p=self.root/pid; p.mkdir()
  (p/'exe').symlink_to('/usr/libexec/gnome-session-binary')
  (p/'stat').write_text(pid+' (gnome-session-b) '+' '.join(['S']+['0']*18+['1234']))
  (p/'cgroup').write_text('1:name=systemd:/user.slice/user-%d.slice/session-2.scope\n'%self.uid)
  (p/'environ').write_bytes(('DISPLAY='+display+'\0XDG_SESSION_ID=2\0XDG_SESSION_TYPE=x11\0').encode())
  return p
 def resolve(self):
  with patch.object(login,'trusted'):
   return login.session_process_display(self.uid,'2',self.root)
 def test_exact_scope_and_unified_scope(self):
  self.assertEqual(self.resolve(),':7')
  p=self.root/'42'/'cgroup'
  p.write_text('0::/user.slice/user-%d.slice/session-2.scope\n'%self.uid)
  self.assertEqual(self.resolve(),':7')
 def test_unique_display_not_unique_process(self):
  self.fixture('43'); self.assertEqual(self.resolve(),':7')
  self.fixture('44',':8')
  with self.assertRaises(ValueError): self.resolve()
 def test_wrong_scope_and_prefix_rejected(self):
  p=self.root/'42'/'cgroup'
  for value in ['session-20.scope','session-2.scope/child','session-3.scope']:
   p.write_text('0::/user.slice/user-%d.slice/%s\n'%(self.uid,value))
   with self.subTest(value=value),self.assertRaises(ValueError): self.resolve()
 def test_metadata_rejected(self):
  p=self.root/'42'/'environ'; good=p.read_bytes()
  bad=[b'',good.replace(b':7',b'host:7'),good.replace(b'=2',b'=3'),good.replace(b'x11',b'wayland'),good+b'DISPLAY=:7\0',good+b'XDG_SESSION_ID=2\0',b'x'*1048577]
  for value in bad:
   p.write_bytes(value)
   with self.subTest(value=value[:70]),self.assertRaises(ValueError): self.resolve()
 def test_wrong_executable_owner_and_missing_process(self):
  p=self.root/'42'/'exe'; p.unlink(); p.symlink_to('/usr/bin/sleep')
  with self.assertRaises(ValueError): self.resolve()
  p.unlink(); p.symlink_to('/usr/libexec/gnome-session-binary')
  with patch.object(login,'trusted'),self.assertRaises(ValueError): login.session_process_display(self.uid+1,'2',self.root)
  (self.root/'42'/'stat').unlink()
  with self.assertRaises(ValueError): self.resolve()
 def test_invalid_identity(self):
  p=self.root/'42'/'stat'; p.write_text('bad')
  with self.assertRaises(ValueError): self.resolve()
 def test_process_identity_changes_during_environment_read(self):
  original=Path.open
  def changed(path,*args,**kwargs):
   if path.name=='environ':
    p=self.root/'42'/'stat'
    p.write_text(p.read_text().replace('1234','4321'))
   return original(path,*args,**kwargs)
  with patch.object(Path,'open',changed),self.assertRaises(ValueError): self.resolve()
 def test_blank_display_fallback_only_for_local_gdm(self):
  good=dict(Id='2',User=str(self.uid),Seat='seat0',Remote='no',Active='yes',Class='user',Type='x11',Display='',Scope='session-2.scope',Service='gdm-password')
  def run(values):
   props=('\n'.join(k+'='+v for k,v in values.items())+'\n').encode()
   with patch.object(login,'command',side_effect=[('2 %d user seat0 tty2\n'%self.uid).encode(),props]),patch.object(login,'session_process_display',return_value=':7') as fallback:
    result=login.discover(self.uid)
    return result,fallback.call_count
  self.assertEqual(run(good)[0]['Display'],':7')
  for k,v in [('Type','wayland'),('Remote','yes'),('Active','no'),('Class','greeter'),('Service','unknown'),('Display','remote:0')]:
   self.assertEqual(run(dict(good,**{k:v})),(None,0))
if __name__=='__main__': unittest.main()
