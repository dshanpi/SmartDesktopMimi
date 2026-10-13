"""Exercise the production C client configuration and TLS rejection boundary."""
from pathlib import Path
import json,os,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2]
class ClientTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory();cls.base=Path(cls.tmp.name)
  (cls.base/'prefix.h').write_text('extern const char *test_config;\n')
  (cls.base/'probe.c').write_text('#include <stdio.h>\n#include "iot_client.h"\nconst char *test_config;int main(int argc,char**argv){test_config=argv[1];char out[4096];int rc=mimi_iot_request("/v1/devices",NULL,out,sizeof(out));printf("%d\\n%s",rc,out);return 0;}\n')
  source=ROOT/'apps/lv_port_linux/src/apps/iot'
  subprocess.run(['cc','-DMIMI_IOT_CONFIG=test_config','-include',str(cls.base/'prefix.h'),'-I'+str(source),str(cls.base/'probe.c'),str(source/'iot_client.c'),'-ljson-c','-lcurl','-lpthread','-o',str(cls.base/'probe')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def probe(self,path):return subprocess.check_output([str(self.base/'probe'),str(path)],text=True)
 def config(self,endpoint='https://localhost:1',mode=0o600):
  p=self.base/'config.json';p.unlink(missing_ok=True);p.write_text(json.dumps({'endpoint':endpoint,'token':'fixture_only_'+'a'*32}));p.chmod(mode);return p
 def test_absent_configuration_is_explained(self):self.assertIn('not_configured',self.probe(self.base/'missing'))
 def test_public_credentials_are_rejected_without_network(self):self.assertIn('invalid_private_configuration',self.probe(self.config(mode=0o644)))
 def test_symlink_rejected(self):
  p=self.config();link=self.base/'link';link.symlink_to(p);self.assertIn('not_configured',self.probe(link))
 def test_insecure_or_redirectable_origins_rejected(self):
  for origin in ['http://localhost:1','https://user@localhost','https://localhost/path','https://localhost?query=1','https://localhost\r\nAuthorization: other']:
   with self.subTest(origin=origin):self.assertIn('invalid_private_configuration',self.probe(self.config(origin)))
 def test_failed_connection_does_not_echo_credentials(self):
  output=self.probe(self.config());self.assertIn('connection_failed',output);self.assertNotIn('fixture_only_',output)
if __name__=='__main__':unittest.main()
