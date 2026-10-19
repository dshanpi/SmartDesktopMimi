"""Actual schema3 build/sign/preflight and malformed ELF/target rejection.

Ephemeral publishers only. No product install/trust/session changes.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT/'tools/aitapp/aitapp.py'
sys.path.insert(0, str(TOOL.parent))
import ubuntu_native as U
import aitapp as A


class NativeAdmissionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='mimi-native-admit-', dir='/var/lib' if os.geteuid()==0 else None)
        cls.base = Path(cls.tmp.name);cls.base.chmod(0o755)
        cls.source = cls.base/'source'
        cls.run_cli('init', cls.source, '--id', 'com.mimi.native-test', '--name', 'Native test', '--publisher', 'test', '--native-ubuntu')
        cls.run_cli('compile-ubuntu', cls.source, '--receipt', cls.base/'build.json')
        cls.program = cls.source/'bin/app'
        cls.raw = cls.program.read_bytes()
        for name in ('one','wrong'):
            key = cls.base/(name+'.key')
            subprocess.run(['openssl','genpkey','-algorithm','RSA','-pkeyopt','rsa_keygen_bits:2048','-out',str(key)],check=True,capture_output=True)
            subprocess.run(['openssl','pkey','-in',str(key),'-pubout','-out',str(cls.base/(name+'.pem'))],check=True,capture_output=True)
        cls.package = cls.base/'native.aitapp'
        cls.run_cli('build', cls.source, '--key', cls.base/'one.key', '-o', cls.package)
        cls.run_cli('verify-ubuntu', cls.package, '--public-key', cls.base/'one.pem', '--receipt', cls.base/'preflight.json')
        receipt=json.loads((cls.base/'preflight.json').read_text())
        print(json.dumps({'signed_package_sha256':receipt['package_sha256'], 'native_sha256':receipt['executable']['sha256'], 'target':receipt['target'], 'host_libraries':{k:v['sha256'] for k,v in receipt['host_libraries'].items()}}),flush=True)

    @classmethod
    def tearDownClass(cls):cls.tmp.cleanup()

    @classmethod
    def run_cli(cls,*args,ok=True):
        p=subprocess.run([sys.executable,str(TOOL),*[str(a) for a in args]],capture_output=True,text=True,timeout=20)
        if (p.returncode==0) != ok:
            raise AssertionError((args,p.returncode,p.stdout,p.stderr))
        return p

    def setUp(self):
        self.case=tempfile.TemporaryDirectory(dir=self.base)
        self.r=Path(self.case.name)

    def tearDown(self):self.case.cleanup()

    def elf(self, change):
        b=bytearray(self.raw);change(b);p=self.r/'changed';p.write_bytes(b);return p

    def test_signed_schema3_build_verify_and_real_host_dependencies(self):
        p=self.run_cli('verify',self.package,'--public-key',self.base/'one.pem')
        self.assertIn('OK',p.stdout)
        receipt=json.loads((self.base/'preflight.json').read_text())
        self.assertEqual(receipt['target'],U.TARGET)
        self.assertIn('libc.so.6',receipt['host_libraries'])
        self.assertTrue(any(k.startswith('libjson-c.so.') for k in receipt['host_libraries']))

    @unittest.skipUnless(os.geteuid()==0,'actual restricted runner requires isolated root fixture')
    def test_verified_native_executes_under_real_runner(self):
        runner=self.base/'runner'
        subprocess.run(['cc','-std=gnu11','-O2','-Wall','-Wextra','-Werror',str(ROOT/'services/mimi-ubuntu-apps/native_runner.c'),'-o',str(runner)],check=True)
        extracted=self.base/'verified-package';extracted.mkdir(mode=0o755)
        A.safe_extract(self.package,extracted)
        verified=extracted/'bin/app'
        expected=json.loads((self.base/'preflight.json').read_text())['executable']['sha256']
        self.assertEqual(hashlib.sha256(verified.read_bytes()).hexdigest(),expected)
        fd=os.open(verified,os.O_RDONLY)
        try:
            p=subprocess.run([str(runner),'--executable-fd',str(fd),'--action','main','--socket',str(self.base/'absent.sock')],pass_fds=(fd,),capture_output=True,text=True,timeout=7)
        finally:os.close(fd)
        self.assertEqual(p.returncode,0,p.stderr);self.assertTrue(json.loads(p.stdout)['ok'])

    def test_wrong_signature_refused(self):
        self.run_cli('verify-ubuntu',self.package,'--public-key',self.base/'wrong.pem',ok=False)

    def test_tampered_signed_manifest_refused(self):
        out=self.r/'tampered.aitapp'
        with tarfile.open(self.package,'r:gz') as src,tarfile.open(out,'w:gz') as dst:
            for m in src.getmembers():
                b=src.extractfile(m).read()
                if m.name=='manifest.json':b+=b' '
                m.size=len(b);dst.addfile(m,io.BytesIO(b))
        p=self.run_cli('verify-ubuntu',out,'--public-key',self.base/'one.pem',ok=False)
        self.assertIn('checksum',p.stderr)

    def test_target_rejections(self):
        for key,value in [('os','debian'),('architecture','aarch64'),('libc','musl'),('app_api',2),('app_api',True),('profile','unknown')]:
            with self.subTest(key=key,value=value):
                t=dict(U.TARGET);t[key]=value
                with self.assertRaises(ValueError):U.target(t)
        for t in ({},dict(U.TARGET,extra=1),None):
            with self.assertRaises(ValueError):U.target(t)

    def test_missing_manifest_target_refused(self):
        shutil.copytree(self.source,self.r/'source');p=self.r/'source/manifest.json';j=json.loads(p.read_text());del j['target'];p.write_text(json.dumps(j))
        with self.assertRaises(SystemExit):A.load_manifest(p.parent)

    def test_boolean_schema_and_runtime_api_refused(self):
        shutil.copytree(self.source,self.r/'source');p=self.r/'source/manifest.json';j=json.loads(p.read_text())
        for field in ('schema','api'):
            item=json.loads(json.dumps(j))
            if field=='schema':item['schema']=True
            else:item['runtime']['api']=True
            p.write_text(json.dumps(item))
            with self.assertRaises(SystemExit):A.load_manifest(p.parent)

    def test_legacy_schema2_does_not_admit_real_x86_64(self):
        with self.assertRaisesRegex(SystemExit,'AArch64'):A.validate_native_elf(self.program)
        p=self.r/'legacy';self.run_cli('init',p,'--id','com.mimi.legacy','--name','Legacy','--publisher','test','--native')
        m=A.load_manifest(p);self.assertEqual(m['schema'],2);self.assertNotIn('target',m)
        self.run_cli('compile-ubuntu',p,ok=False)

    def test_schema1_sign_verify_unchanged(self):
        p=self.r/'page';self.run_cli('init',p,'--id','com.mimi.page','--name','Page','--publisher','test')
        self.run_cli('build',p,'--key',self.base/'one.key','-o',self.r/'page.aitapp')
        self.run_cli('verify',self.r/'page.aitapp','--public-key',self.base/'one.pem')
        self.run_cli('verify-ubuntu',self.r/'page.aitapp','--public-key',self.base/'one.pem',ok=False)

    def test_wrong_machine_refused(self):
        p=self.elf(lambda b:struct.pack_into('<H',b,18,183))
        with self.assertRaisesRegex(ValueError,'x86_64'):U.Elf(p)

    def test_wrong_interpreter_refused(self):
        p=self.elf(lambda b:b.__setitem__(slice(b.index(U.LOADER.encode()),b.index(U.LOADER.encode())+len(U.LOADER)),b'/tmp/' + b'x'*(len(U.LOADER)-5)))
        with self.assertRaisesRegex(ValueError,'interpreter'):U.Elf(p)

    def test_unknown_dependency_refused(self):
        p=self.elf(lambda b:b.__setitem__(slice(b.index(b'libc.so.6'),b.index(b'libc.so.6')+9),b'evil.so.6'))
        with self.assertRaisesRegex(ValueError,'dependency'):U.Elf(p)

    def test_missing_actual_symbol_refused(self):
        old=b'puts\0' if b'puts\0' in self.raw else b'snprintf\0'
        p=self.elf(lambda b:b.__setitem__(slice(b.index(old),b.index(old)+len(old)),b'X'*(len(old)-1)+b'\0'))
        with self.assertRaisesRegex(ValueError,'unresolved'):U.inspect_host(U.Elf(p))

    def test_missing_actual_version_refused(self):
        old=b'GLIBC_2.2.5'
        p=self.elf(lambda b:b.__setitem__(slice(b.index(old),b.index(old)+len(old)),b'GLIBC_9.9.9'))
        with self.assertRaisesRegex(ValueError,'symbol version'):U.inspect_host(U.Elf(p))

    def test_malformed_program_header_refused(self):
        p=self.elf(lambda b:struct.pack_into('<Q',b,32,2**63))
        with self.assertRaises(ValueError):U.Elf(p)

    def test_truncated_elf_refused(self):
        p=self.r/'short';p.write_bytes(self.raw[:60])
        with self.assertRaises(ValueError):U.Elf(p)

    def test_section_header_deception_not_used_for_admission(self):
        def change(b):
            struct.pack_into('<Q',b,40,0);struct.pack_into('<HHH',b,58,0,0,0)
        p=self.elf(change)
        self.assertEqual(U.Elf(p).imports,U.Elf(self.program).imports)
        self.assertEqual(U.inspect_host(U.Elf(p))['status'],'compatible_preflight_only')

    def test_rpath_binary_refused(self):
        src=self.r/'rpath.c';src.write_text('int main(void){return 0;}')
        p=self.r/'rpath';subprocess.run(['cc',str(src),'-Wl,-rpath,/tmp/untrusted','-o',str(p)],check=True)
        with self.assertRaisesRegex(ValueError,'search path'):U.Elf(p)

    def test_exec_stack_refused(self):
        src=self.r/'stack.c';src.write_text('int main(void){return 0;}')
        p=self.r/'stack';subprocess.run(['cc',str(src),'-Wl,-z,execstack','-o',str(p)],check=True)
        with self.assertRaisesRegex(ValueError,'executable stack'):U.Elf(p)

    def test_missing_host_dependency_refused(self):
        with mock.patch.object(U.subprocess,'run',return_value=mock.Mock(stdout='0 libs found in cache')):
            with self.assertRaisesRegex(ValueError,'host dependency'):U.inspect_host(U.Elf(self.program))

    def test_host_wrong_architecture_refused(self):
        with mock.patch.object(U.platform,'machine',return_value='aarch64'):
            with self.assertRaisesRegex(ValueError,'host architecture'):U.inspect_host(U.Elf(self.program))

    def test_untrusted_host_library_path_refused(self):
        p=self.r/'library';p.write_bytes(b'fake')
        self.r.chmod(0o777)
        with self.assertRaisesRegex(ValueError,'untrusted'):U.controlled(p)
        self.r.chmod(0o700)

    @unittest.skipUnless(os.geteuid()==0,'installer refusal uses root fixture')
    def test_installer_still_refuses_native_until_broker_integrated(self):
        spec=importlib.util.spec_from_file_location('native_installer_test',ROOT/'services/mimi-ubuntu-apps/installer.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
        apps=self.r/'apps';apps.mkdir();cfg=self.r/'config.json';cfg.write_text(json.dumps({'schema':1,'apps_root':str(apps),'trusted_keys':[str(self.base/'one.pem')]}))
        with self.assertRaisesRegex(ValueError,'not yet admitted'):m.Installer(cfg).install(self.package)
        self.assertFalse((apps/'com.mimi.native-test').exists())


if __name__=='__main__':unittest.main()
