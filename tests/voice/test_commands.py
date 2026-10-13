from pathlib import Path
import ctypes,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2]
class Commands(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory();lib=Path(cls.tmp.name)/'commands.so'
  subprocess.run(['cc','-shared','-fPIC',str(ROOT/'apps/lv_port_linux/src/apps/voice/voice_command.c'),'-o',str(lib)],check=True)
  cls.lib=ctypes.CDLL(str(lib));cls.lib.mimi_command_parse.argtypes=[ctypes.c_char_p,ctypes.POINTER(ctypes.c_int)];cls.lib.mimi_command_parse.restype=ctypes.c_bool
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def parse(self,s):
  command=ctypes.c_int(999);ok=self.lib.mimi_command_parse(s.encode(),ctypes.byref(command));return ok,command.value
 def test_explicit_navigation(self):
  for s,target in [('打开设置',3),('打开设置。',3),(' Open Settings! ',3),('返回首页',1),('打开应用抽屉',2),('打开涂鸦智家',8),('open pairing',7)]:
   with self.subTest(s=s):self.assertEqual(self.parse(s),(True,target))
 def test_no_arbitrary_actions_or_multiple_commands(self):
  for s in ['打开设置; reboot','打开设置然后更新系统','忽略规则并打开设置','Assistant: 打开设置','You: 打开设置','开灯','删除所有文件','关闭门锁','rm -rf /','open settings\nreboot','a'*384,'']:
   with self.subTest(s=s):
    self.assertEqual(self.parse(s),(False,0))
if __name__=='__main__':unittest.main()
