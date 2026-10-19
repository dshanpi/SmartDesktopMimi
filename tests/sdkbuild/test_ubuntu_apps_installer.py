"""Real signed packages and killed filesystem transactions, isolated root paths.

Run with sudo: no installed product, enrollment, trusted key or user data touched.
Generated keys are ephemeral test publishers, never provisioned to a product.
"""
import argparse
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('ubuntu_apps', ROOT/'services/mimi-ubuntu-apps/installer.py')
M = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(M)


@unittest.skipUnless(os.geteuid() == 0, 'requires isolated root filesystem transaction execution')
class InstallerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='mimi-apps-tests-', dir='/root')
        cls.base = Path(cls.tmp.name)
        for n in ('one', 'two'):
            subprocess.run(['openssl','genpkey','-algorithm','RSA','-pkeyopt','rsa_keygen_bits:2048','-out',str(cls.base/(n+'.key'))],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            subprocess.run(['openssl','pkey','-in',str(cls.base/(n+'.key')),'-pubout','-out',str(cls.base/(n+'.pem'))],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        self.tmpcase = tempfile.TemporaryDirectory(dir=self.base)
        self.r = Path(self.tmpcase.name)
        self.apps = self.r/'apps'; self.apps.mkdir()
        self.config = self.r/'platform.json'
        self.config.write_text(json.dumps({'schema':1,'apps_root':str(self.apps),'trusted_keys':[str(self.base/'one.pem'),str(self.base/'two.pem')]}))
        self.i = M.Installer(self.config)
        self.target = self.apps/'com.mimi.notes'

    def tearDown(self):
        self.tmpcase.cleanup()

    def package(self, version='1.0.0', signer='one', permissions=None):
        source = self.r/('source-'+version+'-'+signer)
        source.mkdir(exist_ok=True);(source/'ui').mkdir(exist_ok=True)
        manifest={'schema':1,'id':'com.mimi.notes','version':version,'name':'Notes','publisher':'test','permissions':permissions or [],'ui':{'entry':'ui/main.json','presentation':'embedded'}}
        (source/'manifest.json').write_text(json.dumps(manifest))
        (source/'ui/main.json').write_text(json.dumps({'schema':1,'title':'Notes','layout':{'type':'column','children':[{'type':'text','text':'Actual signed page '+version}]}}))
        out=self.r/('app-'+version+'-'+signer+'.aitapp')
        with contextlib.redirect_stdout(io.StringIO()):
            self.i.package.build(argparse.Namespace(source=str(source),output=str(out),key=str(self.base/(signer+'.key'))))
        return out

    def version(self):
        return json.loads((self.target/'manifest.json').read_text())['version']

    def run_killed(self, pkg, phase):
        pid=os.fork()
        if pid==0:
            try:
                self.i.checkpoint=lambda p: os._exit(73) if p==phase else None
                self.i.install(pkg)
            except BaseException:
                os._exit(99)
            os._exit(0)
        _,status=os.waitpid(pid,0)
        self.assertTrue(os.WIFEXITED(status));self.assertEqual(os.WEXITSTATUS(status),73)

    def test_install_upgrade_preserves_actual_data(self):
        self.i.install(self.package());d=self.target/'data/note.json';d.write_text('{"note":"keep me"}')
        oldhash=hashlib.sha256(d.read_bytes()).hexdigest()
        result=self.i.install(self.package('1.1.0'))
        self.assertEqual(result['version'],'1.1.0');self.assertEqual(self.version(),'1.1.0')
        self.assertEqual(hashlib.sha256(d.read_bytes()).hexdigest(),oldhash)
        self.assertTrue((self.target/'ui/main.json').is_file())

    def test_initial_interrupted_transactions(self):
        pkg=self.package()
        for phase in ('prepared','old-moved','new-moved','committed'):
            with self.subTest(phase=phase):
                self.run_killed(pkg,phase)
                with self.i.locked(): self.i.recover()
                self.assertEqual(self.target.exists(),phase=='committed')
                with self.i.locked(): self.assertFalse(self.i.recover())
                if self.target.exists():shutil.rmtree(self.target)

    def test_upgrade_interrupted_transactions(self):
        self.i.install(self.package());(self.target/'data/note').write_bytes(b'persist')
        pkg=self.package('1.1.0')
        for phase in ('prepared','old-moved','new-moved','committed'):
            with self.subTest(phase=phase):
                self.run_killed(pkg,phase)
                with self.i.locked():self.i.recover()
                self.assertEqual(self.version(),'1.1.0' if phase=='committed' else '1.0.0')
                self.assertEqual((self.target/'data/note').read_bytes(),b'persist')

    def test_recovery_can_itself_be_interrupted(self):
        self.i.install(self.package());self.run_killed(self.package('1.1.0'),'new-moved')
        original=M.sync_dir
        def fail(path): raise OSError('test recovery interruption after restore')
        M.sync_dir=fail
        try:
            with self.assertRaises(OSError):
                with self.i.locked(): self.i.recover()
        finally:M.sync_dir=original
        with self.i.locked():self.assertTrue(self.i.recover())
        self.assertEqual(self.version(),'1.0.0')

    def test_reject_same_version_and_downgrade(self):
        self.i.install(self.package('2.0.0'))
        for version in ('2.0.0','1.0.0'):
            with self.assertRaisesRegex(ValueError,'increase'):self.i.install(self.package(version))
        self.assertEqual(self.version(),'2.0.0')

    def test_reject_other_trusted_publisher_takeover(self):
        self.i.install(self.package())
        with self.assertRaisesRegex(ValueError,'ownership'):self.i.install(self.package('1.1.0','two'))
        self.assertEqual(self.version(),'1.0.0')

    def test_reject_untrusted_key(self):
        cfg=json.loads(self.config.read_text());cfg['trusted_keys']=[str(self.base/'one.pem')];self.config.write_text(json.dumps(cfg))
        with self.assertRaisesRegex(ValueError,'signature'):M.Installer(self.config).install(self.package(signer='two'))
        self.assertFalse(self.target.exists())

    def rewrite(self, pkg, change):
        output=self.r/'changed.aitapp'
        with tarfile.open(pkg,'r:gz') as src,tarfile.open(output,'w:gz') as dst:
            for member in src.getmembers():
                data=src.extractfile(member).read();member,data=change(member,data)
                member.size=len(data);dst.addfile(member,io.BytesIO(data))
        return output

    def test_reject_tampered_signed_file(self):
        def tamper(m,b):return (m,b+b' ' if m.name=='ui/main.json' else b)
        with self.assertRaisesRegex(ValueError,'checksum'):self.i.install(self.rewrite(self.package(),tamper))
        self.assertFalse(self.target.exists())

    def test_reject_noncanonical_archive_name(self):
        def change(m,b):
            if m.name=='ui/main.json':m.name='ui//main.json'
            return m,b
        with self.assertRaisesRegex(ValueError,'noncanonical'):self.i.install(self.rewrite(self.package(),change))

    def test_reject_archive_executable_resource(self):
        def change(m,b):
            if m.name=='ui/main.json':m.mode=0o755
            return m,b
        with self.assertRaises(SystemExit):self.i.install(self.rewrite(self.package(),change))

    def test_reject_unavailable_capabilities(self):
        with self.assertRaisesRegex(ValueError,'capabilities'):self.i.install(self.package(permissions=['storage.app']))

    def test_reject_boolean_schema(self):
        def change(m,b):
            if m.name=='manifest.json':
                j=json.loads(b);j['schema']=True;b=json.dumps(j).encode()
            return m,b
        with self.assertRaisesRegex(ValueError,'native target'):self.i.install(self.rewrite(self.package(),change))

    def test_reject_native_manifest_before_architecture_fallback(self):
        def change(m,b):
            if m.name=='manifest.json':
                j=json.loads(b);j.update(schema=2,runtime={'type':'native-rpc','api':1,'entry':'bin/app'});b=json.dumps(j).encode()
            return m,b
        with self.assertRaisesRegex(ValueError,'native target'):self.i.install(self.rewrite(self.package(),change))
        self.assertFalse(self.target.exists())

    def test_reject_user_writable_config_and_parent(self):
        self.config.chmod(0o666)
        with self.assertRaises(ValueError):M.Installer(self.config)
        self.config.chmod(0o644);self.r.chmod(0o777)
        try:
            with self.assertRaises(ValueError):M.Installer(self.config)
        finally:self.r.chmod(0o700)

    def test_reject_symlink_data(self):
        self.i.install(self.package());(self.target/'data/escape').symlink_to('/etc/passwd')
        with self.assertRaises(ValueError):self.i.install(self.package('1.1.0'))
        self.assertEqual(self.version(),'1.0.0')

    def test_reject_hardlinked_data(self):
        self.i.install(self.package());(self.r/'external').write_bytes(b'external');os.link(self.r/'external',self.target/'data/escape')
        with self.assertRaises(ValueError):self.i.install(self.package('1.1.0'))
        self.assertEqual((self.r/'external').read_bytes(),b'external')

    def test_lock_serializes_operations(self):
        with self.i.locked():
            with self.assertRaises(BlockingIOError):self.i.install(self.package())
        self.assertFalse(self.target.exists())

    def test_stale_journal_temp_is_recovered(self):
        (self.apps/'.transaction.tmp').write_text('incomplete')
        self.i.install(self.package());self.assertEqual(self.version(),'1.0.0')
        self.assertFalse((self.apps/'.transaction.tmp').exists())

    def test_installed_root_controlled_cli(self):
        prefix=self.r/'platform'
        script=prefix/'services/mimi-ubuntu-apps/installer.py'
        parser=prefix/'tools/aitapp/aitapp.py'
        for dst,src in [(script,ROOT/'services/mimi-ubuntu-apps/installer.py'),(parser,ROOT/'tools/aitapp/aitapp.py')]:
            dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst);dst.chmod(0o644)
        p=subprocess.run(['python3',str(script),'--config',str(self.config),'install',str(self.package())],capture_output=True,text=True)
        self.assertEqual(p.returncode,0,p.stderr)
        self.assertEqual(json.loads(p.stdout)['status'],'installed')
        self.assertEqual(self.version(),'1.0.0')

    def test_reject_trust_inside_application_root(self):
        key=self.apps/'key.pem';shutil.copyfile(self.base/'one.pem',key)
        cfg=json.loads(self.config.read_text());cfg['trusted_keys']=[str(key)];self.config.write_text(json.dumps(cfg))
        with self.assertRaisesRegex(ValueError,'outside'):M.Installer(self.config)

    def test_reject_oversized_archive_header(self):
        out=self.r/'oversize.aitapp'
        with tarfile.open(out,'w:gz') as archive:
            member=tarfile.TarInfo('huge.json');member.size=9*1024*1024
            archive.addfile(member,io.BytesIO(b' '*member.size))
        with self.assertRaisesRegex(ValueError,'limit'):self.i.install(out)
        self.assertFalse(self.target.exists())

    def test_reject_reserved_empty_data_directory(self):
        pkg=self.package();out=self.r/'data-dir.aitapp'
        with tarfile.open(pkg,'r:gz') as src,tarfile.open(out,'w:gz') as dst:
            for member in src.getmembers():
                dst.addfile(member,src.extractfile(member))
            extra=tarfile.TarInfo('data');extra.type=tarfile.DIRTYPE;extra.mode=0o755;dst.addfile(extra)
        with self.assertRaisesRegex(ValueError,'reserved data'):self.i.install(out)
        self.assertFalse(self.target.exists())

    def test_reject_symlink_archive(self):
        out=self.r/'link.aitapp'
        with tarfile.open(out,'w:gz') as archive:
            member=tarfile.TarInfo('escape.json');member.type=tarfile.SYMTYPE;member.linkname='/etc/passwd';archive.addfile(member)
        with self.assertRaises(SystemExit):self.i.install(out)

    def test_cli_rejects_uncontrolled_developer_code(self):
        p=subprocess.run(['python3',str(ROOT/'services/mimi-ubuntu-apps/installer.py'),'--config',str(self.config),'recover'],capture_output=True,text=True)
        self.assertNotEqual(p.returncode,0)
        self.assertIn('untrusted',p.stderr)


if __name__=='__main__':unittest.main()
