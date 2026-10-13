import importlib.util
import json
import shutil
import subprocess
from pathlib import Path
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('baseline',ROOT/'tools/update/baseline.py')
baseline=importlib.util.module_from_spec(spec);spec.loader.exec_module(baseline)

class BaselineSealTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.path=Path(self.temp.name);self.root=self.path/'rootfs'
        config=self.root/'etc/smartdesktopmimi';config.mkdir(parents=True)
        baseline.write(config/'device.json',{'enrollment_id':''})
        factory=self.root/'usr/lib/smartdesktopmimi/factory';factory.mkdir(parents=True)
        baseline.write(factory/'manifest.json',{'commit':'a'*40,'package_sha256':'b'*64})
        self.seal={'schema_version':1,'policy':'MIMI-UPDATE-001/1.0.0','factory_commit':'a'*40,'factory_package_sha256':'b'*64,'files':baseline.inventory(self.root)}
        baseline.write(self.path/'baseline-lock.json',self.seal)
    def test_rejects_modified_content_and_modes(self):
        baseline.verify(self.path)
        device=self.root/'etc/smartdesktopmimi/device.json'
        device.chmod(0o666)
        with self.assertRaises(ValueError):baseline.verify(self.path)
        device.chmod(0o644);device.write_text('{}')
        with self.assertRaises(ValueError):baseline.verify(self.path)
    def test_rejects_credential_partition_even_if_resealed(self):
        p=self.root/'overlay/factory';p.mkdir(parents=True)
        self.seal['files']=baseline.inventory(self.root);baseline.write(self.path/'baseline-lock.json',self.seal)
        with self.assertRaisesRegex(ValueError,'factory data'):baseline.verify(self.path)
    def test_rejects_unit_enrollment_in_generic_image(self):
        baseline.write(self.root/'etc/smartdesktopmimi/device.json',{'enrollment_id':'must-remain-device-local'})
        self.seal['files']=baseline.inventory(self.root);baseline.write(self.path/'baseline-lock.json',self.seal)
        with self.assertRaisesRegex(ValueError,'unit-specific'):baseline.verify(self.path)
    def test_rejects_symbolic_link_to_external_data(self):
        (self.root/'etc/escape').symlink_to('/etc/passwd')
        with self.assertRaisesRegex(ValueError,'symbolic'):baseline.verify(self.path)

    def final_rootfs(self):
        final=self.path/'final';shutil.copytree(self.root,final)
        for source,target in [('aitvbox.init','etc/init.d/aitvbox'),('start-ui.sh','usr/bin/aitvbox-start-ui')]:
            dest=final/target;dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(ROOT/'packaging/aitvbox-suite/files'/source,dest);dest.chmod(0o755)
        rc=final/'etc/rc.d';rc.mkdir()
        for name,service in [('S98mimi-update','mimi-update'),('S99aitvbox','aitvbox'),('S99mimi-desktop','mimi-desktop')]:
            (rc/name).symlink_to('../init.d/'+service)
        for path in final.rglob('*'):
            if path.is_dir():path.chmod(0o755)
        return final

    def test_final_rootfs_roundtrip_and_modified_signed_file(self):
        final=self.final_rootfs();baseline.verify_rootfs(self.path,final)
        (final/'usr/lib/smartdesktopmimi/factory/manifest.json').write_text('{}')
        with self.assertRaisesRegex(ValueError,'differs'):baseline.verify_rootfs(self.path,final)

    def test_final_rootfs_rejects_unsigned_extra_library(self):
        final=self.final_rootfs()
        (final/'usr/lib/smartdesktopmimi/factory/extra.so').write_bytes(b'unsigned')
        (final/'usr/lib/smartdesktopmimi/factory/extra.so').chmod(0o644)
        with self.assertRaisesRegex(ValueError,'unexpected'):baseline.verify_rootfs(self.path,final)

    def test_final_rootfs_rejects_symlinked_parent(self):
        final=self.final_rootfs();shutil.rmtree(final/'usr/lib/smartdesktopmimi/factory')
        (final/'usr/lib/smartdesktopmimi/factory').symlink_to(self.root/'usr/lib/smartdesktopmimi/factory')
        with self.assertRaisesRegex(ValueError,'link'):baseline.verify_rootfs(self.path,final)

    def test_final_rootfs_rejects_stale_or_missing_boot_entry(self):
        final=self.final_rootfs();link=final/'etc/rc.d/S99mimi-desktop'
        link.rename(link.with_name('S100mimi-desktop'))
        with self.assertRaisesRegex(ValueError,'missing enabled'):baseline.verify_rootfs(self.path,final)
        link.symlink_to('../init.d/mimi-desktop')
        with self.assertRaisesRegex(ValueError,'stale service'):baseline.verify_rootfs(self.path,final)

    def test_final_rootfs_rejects_wrong_service_target_and_launcher(self):
        final=self.final_rootfs();link=final/'etc/rc.d/S99mimi-desktop';link.unlink();link.symlink_to('../init.d/aitvbox')
        with self.assertRaisesRegex(ValueError,'wrong service'):baseline.verify_rootfs(self.path,final)
        link.unlink();link.symlink_to('../init.d/mimi-desktop')
        (final/'usr/bin/aitvbox-start-ui').write_text('#!/bin/sh\nexec /usr/bin/lvglsim\n')
        with self.assertRaisesRegex(ValueError,'stale desktop'):baseline.verify_rootfs(self.path,final)

    def test_final_rootfs_rejects_baked_enrollment(self):
        final=self.final_rootfs();enrollment=final/'overlay/smartdesktopmimi-platform/enrollment.json'
        enrollment.parent.mkdir(parents=True);enrollment.write_text('{}')
        with self.assertRaisesRegex(ValueError,'unit-specific'):baseline.verify_rootfs(self.path,final)

    def test_actual_ext4_and_squashfs_readback(self):
        final=self.final_rootfs()
        for kind in ['ext4','squashfs']:
            with self.subTest(filesystem=kind):
                image=self.path/(kind+'.img');extracted=self.path/('readback-'+kind)
                if kind=='ext4':
                    with image.open('wb') as stream:stream.truncate(8*1024*1024)
                    subprocess.run(['fakeroot','mkfs.ext4','-F','-q','-d',str(final),str(image)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                    extracted.mkdir()
                    subprocess.run(['debugfs','-R','rdump / '+str(extracted),str(image)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                else:
                    subprocess.run(['mksquashfs',str(final),str(image),'-all-root','-noappend','-no-progress','-processors','1'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                    subprocess.run(['unsquashfs','-no-progress','-d',str(extracted),str(image)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                baseline.verify_rootfs(self.path,extracted,image)
                if kind=='ext4':
                    subprocess.run(['debugfs','-w','-R','set_inode_field /etc/smartdesktopmimi uid 1000',str(image)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                    with self.assertRaisesRegex(ValueError,'not root-owned'):baseline.verify_rootfs(self.path,extracted,image)
                else:
                    wrong=self.path/'wrong-owner.squashfs'
                    subprocess.run(['mksquashfs',str(final),str(wrong),'-force-uid','1000','-force-gid','1000','-noappend','-no-progress','-processors','1'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
                    with self.assertRaisesRegex(ValueError,'not root-owned'):baseline.verify_rootfs(self.path,extracted,wrong)
                (extracted/'etc/smartdesktopmimi/device.json').write_text('{}')
                with self.assertRaisesRegex(ValueError,'differs'):baseline.verify_rootfs(self.path,extracted)

    def test_package_recipe_preserves_sealed_payload(self):
        # Execute the production install recipe with a minimal OpenWrt macro
        # harness. The real SDK image build remains a separate acceptance step.
        binary=self.root/'usr/lib/smartdesktopmimi/factory/bin/lvglsim'
        binary.parent.mkdir();shutil.copy2('/bin/true',binary)
        self.seal['factory_version']='1.0.0-host-fixture'
        self.seal['files']=baseline.inventory(self.root);baseline.write(self.path/'baseline-lock.json',self.seal)
        product=self.path/'product';prepared=product/'build/mimi-baseline'
        shutil.copytree(self.root,prepared/'rootfs');shutil.copy2(self.path/'baseline-lock.json',prepared/'baseline-lock.json')
        updater=product/'build/mimi-update/mimi-update';updater.parent.mkdir();shutil.copy2('/bin/true',updater)
        script=product/'tools/update/baseline.py';script.parent.mkdir(parents=True);shutil.copy2(ROOT/'tools/update/baseline.py',script)
        top=self.path/'make-top';(top/'include').mkdir(parents=True)
        (top/'rules.mk').write_text('INCLUDE_DIR:='+str(top/'include')+'\nINSTALL_DIR:=install -d -m 0755\nINSTALL_BIN:=install -m 0755\nCP:=cp -fpR\nRSTRIP:=touch '+str(self.path/'STRIP-WAS-CALLED')+'\n')
        (top/'include/package.mk').write_text('')
        stage=self.path/'package';makefile=self.path/'recipe.mk'
        makefile.write_text('TOPDIR:='+str(top)+'\nAITVBOX_PRODUCT_BUILD:='+str(product)+'\ninclude '+str(ROOT/'packaging/aitvbox-mimi-update/Makefile')+'\nall:\n\t$(call Package/aitvbox-mimi-update/install,'+str(stage)+')\n\t$(RSTRIP) '+str(stage)+'\n')
        result=subprocess.run(['make','-f',str(makefile),'all'],cwd=ROOT/'packaging/aitvbox-mimi-update',stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        self.assertEqual(result.returncode,0,result.stdout)
        self.assertFalse((self.path/'STRIP-WAS-CALLED').exists())
        for relative,expected in self.seal['files'].items():self.assertEqual(baseline.digest(stage/relative),expected['sha256'])
        self.assertEqual(baseline.digest(stage/'usr/bin/mimi-update'),baseline.digest(updater))


class SystemPackageIntegrationTests(unittest.TestCase):
    def test_toggle_and_cleanup_remove_updater_but_keep_user_data(self):
        with tempfile.TemporaryDirectory() as temp:
            sdk=Path(temp);(sdk/'.repo').mkdir()
            configs=[sdk/'openwrt/target/a133/a133-b6/defconfig',sdk/'openwrt/openwrt/.config',sdk/'out/a133/b6/openwrt/tmp/.config']
            for p in configs:p.parent.mkdir(parents=True,exist_ok=True);p.write_text('CONFIG_UNRELATED=y\n')
            subprocess.run(['bash',str(ROOT/'scripts/toggle_product.sh'),'on',temp],check=True,stdout=subprocess.DEVNULL)
            for p in configs:self.assertIn('CONFIG_PACKAGE_aitvbox-mimi-update=y',p.read_text())
            out=sdk/'out/a133/b6/openwrt';root=out/'build_dir/target/root-a133-b6'
            for name in ['usr/bin/mimi-update','etc/smartdesktopmimi/trust.json','usr/lib/smartdesktopmimi/factory/bin/lvglsim','etc/init.d/mimi-desktop','overlay/user-data/settings','usr/bin/unrelated']:
                p=root/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('keep-or-clean')
            rc=root/'etc/rc.d';rc.mkdir(parents=True);(rc/'S100mimi-desktop').symlink_to('../init.d/mimi-desktop')
            cache=out/'build_dir/target/aitvbox-mimi-update-1.0.0';cache.mkdir()
            subprocess.run(['bash',str(ROOT/'scripts/toggle_product.sh'),'off',temp],check=True,stdout=subprocess.DEVNULL)
            for p in configs:
                self.assertIn('# CONFIG_PACKAGE_aitvbox-mimi-update is not set',p.read_text());self.assertIn('CONFIG_UNRELATED=y',p.read_text())
            self.assertFalse((root/'usr/bin/mimi-update').exists());self.assertFalse((root/'etc/smartdesktopmimi').exists())
            self.assertFalse((rc/'S100mimi-desktop').is_symlink());self.assertFalse(cache.exists())
            self.assertEqual((root/'overlay/user-data/settings').read_text(),'keep-or-clean')
            self.assertTrue((root/'usr/bin/unrelated').exists())

    def test_desktop_boot_order_follows_backend(self):
        def start(name):
            for line in (ROOT/name).read_text().splitlines():
                if line.startswith('START='):return line.split('=')[1]
            self.fail('missing START')
        services=['S'+start('packaging/aitvbox-mimi-update/files/mimi-update.init')+'mimi-update',
                  'S'+start('packaging/aitvbox-suite/files/aitvbox.init')+'aitvbox',
                  'S'+start('packaging/aitvbox-mimi-update/files/mimi-desktop.init')+'mimi-desktop']
        self.assertEqual(sorted(services),services)

if __name__=='__main__':unittest.main()
