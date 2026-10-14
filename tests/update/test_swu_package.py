"""Real RSA/CPIO host tests with tiny fake images; never a flashable system."""
import gzip
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/update'))
import swu_package as swu


class SWUPackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='swu-host-fixture-');cls.root=Path(cls.temp.name)
        cls.key=cls.root/'private.pem';cls.public=cls.root/'public.pem'
        subprocess.run(['openssl','genpkey','-algorithm','RSA','-pkeyopt','rsa_keygen_bits:3072','-out',str(cls.key)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        cls.key.chmod(0o600)
        cls.public.write_bytes(subprocess.check_output(['openssl','pkey','-in',str(cls.key),'-pubout']))
        cls.template=ROOT/'integrations/swupdate/sw-description-ab-sign-rollback'
    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()

    def inputs(self,root):
        (root/'kernel').write_bytes(b'host kernel fixture')
        (root/'rootfs').write_bytes(b'host rootfs fixture'*300)
        return root/'kernel',root/'rootfs'

    def test_deterministic_signature_archive_and_input_preservation(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);kernel,rootfs=self.inputs(root)
            before=(kernel.read_bytes(),rootfs.read_bytes(),self.key.read_bytes())
            records=[]
            for name in ['one','two']:
                records.append(swu.package(kernel,rootfs,self.template,self.key,self.public,None,root/name,'2.0.0',1700000000,[]))
            self.assertEqual(records[0],records[1])
            self.assertEqual((root/'one/system.swu').read_bytes(),(root/'two/system.swu').read_bytes())
            extracted=root/'extracted';extracted.mkdir()
            with (root/'one/system.swu').open('rb') as source:
                subprocess.run(['cpio','--quiet','-id','--no-absolute-filenames'],stdin=source,cwd=extracted,check=True)
            self.assertEqual(set(p.name for p in extracted.iterdir()),{'sw-description','sw-description.sig','kernel','rootfs'})
            self.assertEqual(gzip.decompress((extracted/'rootfs').read_bytes()),rootfs.read_bytes())
            self.assertEqual((extracted/'kernel').read_bytes(),kernel.read_bytes())
            text=(extracted/'sw-description').read_text()
            self.assertIn('version = "2.0.0";',text)
            self.assertNotIn('"@kernel"',text);self.assertNotIn('"@rootfs"',text)
            for name in ['kernel','rootfs']:self.assertIn(swu.digest(extracted/name),text)
            subprocess.run(['openssl','dgst','-sha256','-verify',str(self.public),'-signature',str(extracted/'sw-description.sig'),str(extracted/'sw-description')],check=True,stdout=subprocess.DEVNULL)
            self.assertEqual(before,(kernel.read_bytes(),rootfs.read_bytes(),self.key.read_bytes()))

    def test_real_shell_wrapper_checks_image_key_before_packaging(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);product=root/'product';sdk=root/'sdk';(sdk/'.repo').mkdir(parents=True)
            for name in ['scripts/build_swu.sh','scripts/lib.sh','tools/update/swu_package.py','tools/update/baseline.py','tools/update/release_identity.py','integrations/swupdate/sw-description-ab-sign-rollback']:
                target=product/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/name,target)
            # This test deliberately uses nonbootable image fixtures to exercise
            # the real RSA/rootfs-key/CPIO boundary. Accept the separate display
            # prerequisite only inside this temporary fixture product; its
            # artifact rejection cases live in test_a133_display_gate.
            display_gate=product/'tools/update/a133_display_gate.py'
            display_gate.write_text("import sys\nassert sys.argv[2:]==['--stage','packed']\n")
            fs=root/'fs';(fs/'etc').mkdir(parents=True);shutil.copy2(self.public,fs/'etc/swupdate_public.pem')
            images=sdk/'out/a133/b6/openwrt';images.mkdir(parents=True)
            (images/'boot.img').write_bytes(b'HOST FIXTURE NOT BOOTABLE')
            with (images/'rootfs.img').open('wb') as output:output.truncate(8*1024*1024)
            subprocess.run(['mkfs.ext4','-F','-q','-d',str(fs),str(images/'rootfs.img')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            env=dict(os.environ,MIMI_SWU_SIGNING_KEY=str(self.key),MIMI_SWU_PUBLIC_KEY=str(self.public),MIMI_SYSTEM_VERSION='9.8.7-host-only',SOURCE_DATE_EPOCH='1700000000')
            command=['bash',str(product/'scripts/build_swu.sh'),str(sdk)]
            result=subprocess.run(command,env=env,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            self.assertEqual(result.returncode,0,result.stdout)
            self.assertTrue((product/'build/swu/9.8.7-host-only/system.swu').is_file())
            subprocess.run(['debugfs','-w','-R','rm /etc/swupdate_public.pem',str(images/'rootfs.img')],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            env['MIMI_SYSTEM_VERSION']='9.8.8-host-only'
            result=subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            self.assertNotEqual(result.returncode,0)
            self.assertFalse((product/'build/swu/9.8.8-host-only').exists())
            self.assertFalse(list(sdk.rglob('*private*')))
            # A failing display prerequisite must stop the real shell entry
            # before any archive is produced, even with a valid signing key.
            display_gate.write_text('raise SystemExit(19)\n')
            env['MIMI_SYSTEM_VERSION']='9.8.9-host-only'
            result=subprocess.run(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            self.assertEqual(result.returncode,19)
            self.assertFalse((product/'build/swu/9.8.9-host-only').exists())

    def test_reject_secret_in_sdk_or_insecure_mode(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);kernel,rootfs=self.inputs(root);sdk=root/'sdk';sdk.mkdir()
            key=sdk/'secret.pem';key.write_bytes(self.key.read_bytes());key.chmod(0o600)
            with self.assertRaisesRegex(ValueError,'outside'):
                swu.package(kernel,rootfs,self.template,key,self.public,None,root/'candidate','1.0.0',1,[sdk])
            self.assertFalse((root/'candidate').exists())
            key.chmod(0o644)
            with self.assertRaisesRegex(ValueError,'private regular'):
                swu.package(kernel,rootfs,self.template,key,self.public,None,root/'candidate','1.0.0',1,[])

    def test_wrong_public_key_and_invalid_version_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);kernel,rootfs=self.inputs(root);other=root/'other.pem';public=root/'other.pub'
            subprocess.run(['openssl','genpkey','-algorithm','RSA','-pkeyopt','rsa_keygen_bits:3072','-out',str(other)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            public.write_bytes(subprocess.check_output(['openssl','pkey','-in',str(other),'-pubout']))
            with self.assertRaisesRegex(ValueError,'mismatch'):
                swu.package(kernel,rootfs,self.template,self.key,public,None,root/'candidate','1.0.0',1,[])
            with self.assertRaisesRegex(ValueError,'version'):
                swu.package(kernel,rootfs,self.template,self.key,self.public,None,root/'candidate','1.0.0"; evil',1,[])
            self.assertFalse((root/'candidate').exists())


if __name__=='__main__':unittest.main()
