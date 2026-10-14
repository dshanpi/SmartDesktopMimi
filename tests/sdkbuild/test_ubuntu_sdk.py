"""Architecture/OS pairing and archive tests, not signed installation acceptance."""
import copy
import json
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch
import test_deb as fixtures
from sdk_targets import target_spec

class UbuntuSDKTests(unittest.TestCase):
    def setUp(self):
        fixtures.SDKTargetTests.setUp(self)
        for name in ('gcc','g++','ar','ranlib','strip'):
            (self.root/'toolchain/bin'/('aarch64-linux-gnu-'+name)).rename(self.root/'toolchain/bin'/('x86_64-linux-gnu-'+name))
        (self.root/'sysroot/usr/lib/os-release').write_text('ID=ubuntu\nVERSION_ID="20.04"\n')
        self.record.update(platform_id='ubuntu-host',architecture='x86_64',target_os='ubuntu',target_os_version='20.04',compiler_prefix='x86_64-linux-gnu-',system_baseline='ubuntu-test-only')
        self.record['files']=fixtures.sdk.inventory(self.root)
    save=fixtures.SDKTargetTests.save
    def profile(self):
        return dict(schema_version=1,platform_id='ubuntu-host',architecture='x86_64',target_os='ubuntu',package_format='deb',sdk_lock_sha256=self.save(),system_baseline='ubuntu-test-only',screens=[dict(width=1024,height=768)],voice_control=False)
    def test_exact_tuple_and_reject_cross_product_combinations(self):
        self.assertEqual(target_spec('ubuntu-host','x86_64','ubuntu')['deb_architecture'],'amd64')
        self.assertEqual(target_spec('a133','aarch64','tina')['deb_architecture'],'arm64')
        for platform,arch,os in [('a133','x86_64','ubuntu'),('ubuntu-host','aarch64','ubuntu'),('ubuntu-host','x86_64','tina'),('ubuntu-host','x86_64','debian'),('t527','aarch64','ubuntu'),('rk3576','aarch64','ubuntu')]:
            with self.subTest(platform=platform,arch=arch,os=os),self.assertRaises(ValueError):target_spec(platform,arch,os)
    def test_pinned_sdk_and_profile(self):
        profile=self.profile();self.assertEqual(fixtures.sdk.verify_sdk(self.root,profile['sdk_lock_sha256'])['architecture'],'x86_64')
        self.assertEqual(fixtures.configure.validate(profile),profile)
        for key,value in [('architecture','aarch64'),('platform_id','a133'),('target_os','debian')]:
            bad=copy.deepcopy(profile);bad[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):fixtures.configure.validate(bad)
    def test_compiler_prefix_and_legacy_schema_cannot_be_relabelled(self):
        for key,value in [('compiler_prefix','aarch64-linux-gnu-'),('schema_version',1),('target_os_version','22.04')]:
            old=self.record[key];self.record[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):fixtures.sdk.verify_sdk(self.root,self.save())
            self.record[key]=old
    def test_native_headers_and_wrong_compiler_machine(self):
        for name in ('alsa/asoundlib.h','json-c/json.h','x86_64-linux-gnu/curl/curl.h','SDL2/SDL.h'):
            p=self.root/'sysroot/usr/include'/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('fixture')
        self.record['files']=fixtures.sdk.inventory(self.root);profile=self.profile()
        with patch.object(fixtures.configure.subprocess,'check_output',return_value='x86_64-linux-gnu\n'):
            self.assertEqual(fixtures.configure.check_sdk(profile,self.root,False)['dependency_link'],'not_run')
        with patch.object(fixtures.configure.subprocess,'check_output',return_value='aarch64-linux-gnu\n'),self.assertRaisesRegex(ValueError,'target differs'):
            fixtures.configure.check_sdk(profile,self.root,False)
    def test_capture_runtime_requires_real_pinned_libraries(self):
        self.record['capture']=dict(kind='installed-package-input-snapshot',host_runtime={'/etc/passwd':'a'*64})
        with self.assertRaisesRegex(ValueError,'Invalid SDK compiler'):fixtures.sdk.verify_sdk(self.root,self.save())
        self.record['capture']['host_runtime']={'/lib/x86_64-linux-gnu/libc.so.6':'a'*64}
        with self.assertRaisesRegex(ValueError,'runtime changed'):fixtures.sdk.verify_sdk(self.root,self.save())
        self.record['capture']['host_runtime']={}
        with self.assertRaisesRegex(ValueError,'runtime lock required'):fixtures.sdk.verify_sdk(self.root,self.save())

class UbuntuDebTests(unittest.TestCase):
    def setUp(self):
        fixtures.DebianBundleTests.setUp(self)
        self.profile.update(platform_id='ubuntu-host',architecture='x86_64',target_os='ubuntu',system_baseline='ubuntu-test-only')
        self.sdk.update(target_os_version='20.04')
        self.receipt.update(**{k:self.profile[k] for k in ('platform_id','architecture','target_os','system_baseline')},target_os_version='20.04')
        target=self.manifest['targets'][0];target['identity'].update(platform_id='ubuntu-host',architecture='x86_64');target['capabilities']=['target-os:ubuntu:20.04']
        self.profile_path.write_text(json.dumps(self.profile));self.refresh_bundle()
    refresh_bundle=fixtures.DebianBundleTests.refresh_bundle
    build=fixtures.DebianBundleTests.build
    check=fixtures.DebianBundleTests.check
    def test_actual_deb_archive_is_amd64_and_has_no_activation_scripts(self):
        self.check();self.build();archives=list((self.root/'output').glob('*.deb'));self.assertEqual(len(archives),1)
        self.assertTrue(archives[0].name.endswith('_amd64.deb'))
        self.assertEqual(subprocess.check_output(['dpkg-deb','-f',str(archives[0]),'Architecture'],text=True).strip(),'amd64')
        control=self.root/'control';subprocess.run(['dpkg-deb','-e',str(archives[0]),str(control)],check=True)
        self.assertEqual({p.name for p in control.iterdir()},{'control'})
        contents=subprocess.check_output(['dpkg-deb','-c',str(archives[0])],text=True)
        self.assertIn('bundle-ubuntu-host/manifest.sig',contents);self.assertNotIn('provider',contents);self.assertNotIn('/etc/',contents)
    def test_arm64_or_wrong_os_signed_target_rejected(self):
        for key,value in [('architecture','aarch64'),('platform_id','a133')]:
            identity=self.manifest['targets'][0]['identity'];old=identity[key];identity[key]=value
            with self.assertRaises(ValueError):self.check()
            identity[key]=old
        self.manifest['targets'][0]['capabilities']=['target-os:ubuntu:22.04']
        with self.assertRaises(ValueError):self.check()

if __name__=='__main__':unittest.main()
