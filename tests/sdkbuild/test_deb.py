"""Host archive/schema tests. Fixtures are not a qualified Debian SDK or board."""
from pathlib import Path
import copy
import hashlib
import io
import json
import os
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/sdkbuild'))
import package_deb as deb
import configure
import application_sdk as sdk


class SDKTargetTests(unittest.TestCase):
    def test_host_include_library_and_pkgconfig_overrides_do_not_leak(self):
        dirty={'CPATH':'/host/include','LIBRARY_PATH':'/host/lib','PKG_CONFIG_PATH':'/host/pkgconfig','GCC_EXEC_PREFIX':'/host/tools','MIMI_DISPLAY_PROFILES':'480x800'}
        with patch.dict(os.environ,dirty):
            clean=sdk.compiler_environment()
        for key in dirty:
            if key!='MIMI_DISPLAY_PROFILES':self.assertNotIn(key,clean)
        self.assertEqual(clean['MIMI_DISPLAY_PROFILES'],'480x800')
    def setUp(self):
        t = tempfile.TemporaryDirectory(); self.addCleanup(t.cleanup); self.root = Path(t.name)
        (self.root/'toolchain/bin').mkdir(parents=True)
        (self.root/'sysroot/usr/lib').mkdir(parents=True)
        (self.root/'sysroot/usr/lib/os-release').write_text('ID=debian\nVERSION_ID="12"\n')
        for name in ('gcc', 'g++', 'ar', 'ranlib', 'strip'):
            (self.root/'toolchain/bin'/('aarch64-linux-gnu-'+name)).write_text('host-only fixture')
        self.record = dict(schema_version=2, platform_id='a133', architecture='aarch64',
                           system_baseline='fixture-debian-12', target_os='debian', target_os_version='12',
                           compiler_prefix='aarch64-linux-gnu-', files=sdk.inventory(self.root))
    def save(self):
        p=self.root/'sdk-lock.json';p.write_text(json.dumps(self.record));return sdk.digest(p)
    def test_explicit_target_matches_inventory(self):
        r=sdk.verify_sdk(self.root,self.save());self.assertEqual(sdk.sdk_target(r,self.root)['target_os'],'debian')
    def test_cannot_relabel_legacy_tina(self):
        self.record['schema_version']=1
        with self.assertRaisesRegex(ValueError,'Tina only'):sdk.verify_sdk(self.root,self.save())
    def test_wrong_os_release_prefix_version_and_platform(self):
        for key,value in [('target_os','ubuntu'),('target_os_version','24.04'),('compiler_prefix','../../host-'),('platform_id','t527')]:
            with self.subTest(key=key):
                old=self.record[key];self.record[key]=value
                with self.assertRaises(ValueError):sdk.verify_sdk(self.root,self.save())
                self.record[key]=old
    def test_os_file_change_requires_new_reviewed_pin(self):
        pin=self.save();(self.root/'sysroot/usr/lib/os-release').write_text('ID=ubuntu\nVERSION_ID=24.04\n')
        with self.assertRaisesRegex(ValueError,'differs'):sdk.verify_sdk(self.root,pin)
    def test_missing_target_compiler_rejected(self):
        (self.root/'toolchain/bin/aarch64-linux-gnu-g++').unlink()
        self.record['files']=sdk.inventory(self.root)
        with self.assertRaisesRegex(ValueError,'tool missing'):sdk.verify_sdk(self.root,self.save())
    def test_multiarch_headers_and_exact_compiler_target(self):
        for header in ('alsa/asoundlib.h','json-c/json.h','libevdev-1.0/libevdev/libevdev.h','aarch64-linux-gnu/curl/curl.h'):
            p=self.root/'sysroot/usr/include'/header;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('fixture')
        self.record['files']=sdk.inventory(self.root);pin=self.save()
        profile=dict(schema_version=1,platform_id='a133',target_os='debian',package_format='deb',architecture='aarch64',
                     sdk_lock_sha256=pin,system_baseline='fixture-debian-12',screens=[dict(width=1024,height=768)],voice_control=False)
        with patch.object(configure.subprocess,'check_output',return_value='aarch64-linux-gnu\n'):
            self.assertEqual(configure.check_sdk(profile,self.root,False)['dependency_link'],'not_run')
        with patch.object(configure.subprocess,'check_output',return_value='aarch64-openwrt-linux-gnu\n'):
            with self.assertRaisesRegex(ValueError,'target differs'):configure.check_sdk(profile,self.root,False)


class DebianBundleTests(unittest.TestCase):
    def setUp(self):
        t=tempfile.TemporaryDirectory();self.addCleanup(t.cleanup);self.root=Path(t.name)
        self.profile=dict(schema_version=1,platform_id='a133',target_os='debian',package_format='deb',
                          architecture='aarch64',sdk_lock_sha256='a'*64,system_baseline='fixture-debian-12',
                          screens=[dict(width=1024,height=768)],voice_control=False)
        self.sdk=dict(schema_version=2,target_os_version='12')
        self.receipt=dict(schema_version=1,kind='application-build',**{k:self.profile[k] for k in ('platform_id','target_os','architecture','sdk_lock_sha256','system_baseline')},
                          target_os_version='12',source=dict(commit='b'*40,dirty=False),
                          build=dict(application_only=True,cloud_provider=False,target='lvglsim',display_profiles=['1024x768'],voice_control=False),desktop_sha256=hashlib.sha256(b'NOT A TARGET BINARY').hexdigest())
        self.manifest=dict(release_type='application',version='1.0.8-rc.12',commit='b'*40,
                           targets=[dict(identity=dict(product_id='smartdesktopmimi',platform_id='a133',architecture='aarch64'),capabilities=['target-os:debian:12'])],
                           files={'bin/lvglsim':{'sha256':self.receipt['desktop_sha256']}})
        self.bundle=self.root/'bundle';self.bundle.mkdir()
        self.profile_path=self.root/'profile.json';self.profile_path.write_text(json.dumps(self.profile))
        # Explicit stub verifier is ONLY for archive tests. No trusted keys or
        # production signing operations are used; separate rejection tests run.
        self.verifier=self.root/'verifier';self.verifier.write_text('#!/bin/sh\nexit 0\n');self.verifier.chmod(0o755)
        self.refresh_bundle()
    def refresh_bundle(self):
        raw=json.dumps(self.receipt).encode();self.receipt_hash=hashlib.sha256(raw).hexdigest()
        self.manifest['files']['resources/application-build.json']={'sha256':self.receipt_hash}
        with tarfile.open(self.bundle/'application.tar.gz','w:gz') as tar:
            for name,data in [('resources/application-build.json',raw),('bin/lvglsim',b'NOT A TARGET BINARY')]:
                member=tarfile.TarInfo(name);member.size=len(data);tar.addfile(member,io.BytesIO(data))
        self.manifest['package_sha256']=deb.digest(self.bundle/'application.tar.gz')
        (self.bundle/'manifest.json').write_text(json.dumps(self.manifest));(self.bundle/'manifest.sig').write_text('HOST FIXTURE NOT A SIGNATURE')
    def check(self):deb.check_binding(self.profile,self.sdk,self.manifest,self.receipt,self.receipt_hash)
    def build(self,output='output'):
        with patch.object(deb,'check_sdk',return_value={'fixture':True}),patch.object(deb,'verify_sdk',return_value=self.sdk):
            return deb.build_deb(self.profile_path,self.root/'sdk',self.bundle,self.verifier,self.root/'trust',self.root/'device',self.root/output,1700000000,'Host Fixture <fixture@example.invalid>')
    def test_version_ordering_preserves_prerelease_before_stable(self):
        subprocess.run(['dpkg','--compare-versions',deb.deb_version('1.0.8-rc.12'),'lt',deb.deb_version('1.0.8')],check=True)
        self.assertEqual(deb.deb_version('1.0.8'),'1.0.8-1')
        for value in ['v1.0.0','1.2.3\nDepends: bad','../../x','01.2.3']:
            with self.assertRaises(ValueError):deb.deb_version(value)
    def test_exact_signed_build_binding(self):self.check()
    def test_rejects_wrong_os_type_target_source_and_payload(self):
        mutations=[('type',lambda:self.manifest.update(release_type='system')),
                   ('os',lambda:self.receipt.update(target_os='tina')),
                   ('os_version',lambda:self.receipt.update(target_os_version='11')),
                   ('source',lambda:self.receipt['source'].update(commit='c'*40)),
                   ('dirty',lambda:self.receipt['source'].update(dirty=True)),
                   ('sdk',lambda:self.receipt.update(sdk_lock_sha256='c'*64)),
                   ('screen',lambda:self.receipt['build'].update(display_profiles=['480x800'])),
                   ('voice',lambda:self.receipt['build'].update(voice_control=True)),
                   ('desktop',lambda:self.receipt.update(desktop_sha256='d'*64)),
                   ('capability',lambda:self.manifest['targets'][0].update(capabilities=[])),
                   ('platform',lambda:self.manifest['targets'][0]['identity'].update(platform_id='rk3576'))]
        for name,mutate in mutations:
            with self.subTest(name=name):
                original=copy.deepcopy((self.manifest,self.receipt));mutate()
                with self.assertRaises(ValueError):self.check()
                self.manifest,self.receipt=original
    def test_tina_real_profile_is_rejected_before_verifier(self):
        self.profile.update(target_os='tina',package_format='mimi-application');self.profile_path.write_text(json.dumps(self.profile))
        with patch.object(deb.subprocess,'run') as command,self.assertRaisesRegex(ValueError,'relabelled'):
            self.build()
        command.assert_not_called()
    def test_verification_failure_leaves_no_package(self):
        self.verifier.write_text('#!/bin/sh\nexit 17\n')
        with self.assertRaises(subprocess.CalledProcessError):self.build()
        self.assertFalse((self.root/'output').exists())
    def test_missing_signature_symlink_and_mismatch_rejected(self):
        (self.bundle/'manifest.sig').unlink()
        with self.assertRaisesRegex(ValueError,'regular file'):self.build()
        (self.bundle/'manifest.sig').symlink_to(self.bundle/'manifest.json')
        with self.assertRaisesRegex(ValueError,'regular file'):self.build()
        (self.bundle/'manifest.sig').unlink();self.refresh_bundle()
        self.manifest['package_sha256']='0'*64;(self.bundle/'manifest.json').write_text(json.dumps(self.manifest))
        with self.assertRaisesRegex(ValueError,'Archive hash'):self.build()
    def test_archive_determinism_metadata_ownership_and_no_install_scripts(self):
        first=self.build('one');second=self.build('two');self.assertEqual(first['deb_sha256'],second['deb_sha256'])
        package=next((self.root/'one').glob('*.deb'))
        fields=subprocess.check_output(['dpkg-deb','-f',str(package),'Package','Version','Architecture'],text=True)
        self.assertIn('smartdesktopmimi-bundle-a133',fields);self.assertIn('arm64',fields)
        extracted=self.root/'extracted';subprocess.run(['dpkg-deb','-R',str(package),str(extracted)],check=True)
        self.assertEqual({p.name for p in (extracted/'DEBIAN').iterdir()},{'control'})
        payload=extracted/'usr/share/smartdesktopmimi/bundle-a133'
        self.assertEqual(deb.digest(payload/'application.tar.gz'),self.manifest['package_sha256'])
        self.assertFalse((extracted/'etc').exists());self.assertFalse((extracted/'var').exists())
        data=subprocess.check_output(['dpkg-deb','--fsys-tarfile',str(package)])
        with tarfile.open(fileobj=io.BytesIO(data)) as tar:
            for m in tar:self.assertEqual((m.uid,m.gid),(0,0));self.assertFalse(m.issym() or m.islnk())
        self.assertEqual(first['activation'],'not_run');self.assertFalse(first['uploaded'])
    def test_portable_asset_filename_keeps_real_dpkg_version_and_signed_bytes(self):
        for index, version in enumerate(('1.0.8-rc.29', '1.0.8-rc.29+build.4', '1.0.8')):
            with self.subTest(version=version):
                self.manifest['version']=version;self.refresh_bundle()
                record=self.build('portable-'+str(index))
                package=next((self.root/('portable-'+str(index))).glob('*.deb'))
                self.assertRegex(package.name, r'^[A-Za-z0-9._-]+$')
                self.assertEqual(subprocess.check_output(['dpkg-deb','-f',str(package),'Version'],text=True).strip(),deb.deb_version(version))
                data=subprocess.check_output(['dpkg-deb','--fsys-tarfile',str(package)])
                with tarfile.open(fileobj=io.BytesIO(data)) as tar:
                    raw=tar.extractfile('./usr/share/smartdesktopmimi/bundle-a133/application.tar.gz').read()
                self.assertEqual(hashlib.sha256(raw).hexdigest(),record['application_sha256'])

    def test_no_overwrite_existing_output(self):
        self.build()
        with self.assertRaisesRegex(ValueError,'already exists'):self.build()


if __name__=='__main__':unittest.main()
