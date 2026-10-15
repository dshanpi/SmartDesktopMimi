"""Package source editions must agree before any rootfs content is copied."""
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
import check_product_edition as c


class ProductEditionTests(unittest.TestCase):
    def fixture(self, root, edition):
        build = root / 'build'
        (build / 'lv_port_linux').mkdir(parents=True)
        (build / 'tuyaopen').mkdir()
        header = b'\x7fELF\x02\x01' + b'\x00' * 12 + b'\xb7\x00'
        backend = build / 'lv_port_linux/lv_backend'
        backend.write_bytes(header + b'fixture backend ' + edition.encode())
        provider = dict(requirement='REQ-MIMI-PRIVATE-066', status='passed', phase='compiled',
                        profile=edition, recipe_sha256=c.RECIPES[edition], provider_edition_source_version=2,
                        original_source_dependency=False, reference_backend_sha256=c.sha(backend), backend_sha256=c.sha(backend))
        if edition == 'private':
            provider.update(private_product_source_lock_sha256=c.PRIVATE_LOCK_HASH,
                            private_tuya_source_lock_sha256=c.TUYA_LOCK_HASH, historical_sdk_pairing_sources_used=False)
        (build / 'lv_port_linux/provider-build-result.json').write_text(json.dumps(provider))
        _, runtime = c.tuya.selection('public')
        runtime.update(profile=edition, status='passed', phase='compiled')
        if edition == 'private':
            runtime['private_source_lock_sha256'] = c.TUYA_LOCK_HASH
            runtime['files'][c.tuya.APP + 'src/mimi_device_pairing.c'] = c.PRIVATE_RUNTIME
        binary = build / 'tuyaopen/your_chat_bot_QIO_1.0.1.bin'
        binary.write_bytes(header + b'fixture runtime ' + edition.encode())
        runtime.update(binary_sha256=c.sha(binary), binary_bytes=binary.stat().st_size)
        (build / 'tuyaopen/pairing-build-result.json').write_text(json.dumps(runtime))
        return build, {**c.REFERENCE, edition: c.sha(backend)}

    def test_mixed_or_changed_programs_and_records_rejected(self):
        for change in ('provider-profile', 'runtime-profile', 'provider', 'runtime', 'missing', 'duplicate-key', 'architecture'):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as directory:
                build, refs = self.fixture(Path(directory).resolve(), 'public')
                provider_file = build / 'lv_port_linux/provider-build-result.json'
                runtime_file = build / 'tuyaopen/pairing-build-result.json'
                with patch.object(c, 'REFERENCE', refs):
                    c.check(str(build), 'public')
                    if change.endswith('-profile'):
                        selected = provider_file if change == 'provider-profile' else runtime_file
                        data = json.loads(selected.read_text());data['profile'] = 'private'
                        selected.write_text(json.dumps(data))
                    elif change == 'provider':
                        (build / 'lv_port_linux/lv_backend').write_bytes(b'changed')
                    elif change == 'runtime':
                        (build / 'tuyaopen/your_chat_bot_QIO_1.0.1.bin').write_bytes(b'changed')
                    elif change == 'missing':
                        runtime_file.unlink()
                    elif change == 'duplicate-key':
                        runtime_file.write_text('{"profile":"public","profile":"private"}')
                    else:
                        binary = build / 'tuyaopen/your_chat_bot_QIO_1.0.1.bin'
                        binary.write_bytes(b'wrong architecture with updated hash')
                        data = json.loads(runtime_file.read_text())
                        data.update(binary_sha256=c.sha(binary), binary_bytes=binary.stat().st_size)
                        runtime_file.write_text(json.dumps(data))
                    with self.assertRaises(ValueError):
                        c.check(str(build), 'public')

    def test_private_requires_explicit_current_component_locks(self):
        with tempfile.TemporaryDirectory() as directory:
            build, refs = self.fixture(Path(directory).resolve(), 'private')
            with patch.object(c, 'REFERENCE', refs):
                c.check(str(build), 'private')
                record = build / 'lv_port_linux/provider-build-result.json'
                data = json.loads(record.read_text());data['historical_sdk_pairing_sources_used'] = True
                record.write_text(json.dumps(data))
                with self.assertRaisesRegex(ValueError, 'private provider source lock'):
                    c.check(str(build), 'private')

    def test_init_selects_public_private_and_rejects_unknown_without_start(self):
        script = (ROOT / 'packaging/aitvbox-suite/files/aitvbox.init').read_text()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            edition_file = root / 'edition'
            init = root / 'init.sh'
            init.write_text(script.replace('/etc/smartdesktopmimi/product-edition', str(edition_file)))
            for edition in ('', 'public', 'private', 'unknown'):
                with self.subTest(edition=edition):
                    edition_file.write_text(edition)
                    command = '. ' + shlex.quote(str(init)) + '; boot_log() { :; }; select_product_runtime && printf "%s" "$tuya_bin"'
                    result = subprocess.run(['sh', '-c', command], capture_output=True, text=True)
                    if edition == 'unknown':
                        self.assertNotEqual(result.returncode, 0)
                        self.assertEqual(result.stdout, '')
                    else:
                        self.assertEqual(result.returncode, 0)
                        self.assertEqual(result.stdout, '/usr/bin/your_chat_bot_QIO_1.0.1.bin' if edition == 'private'
                                         else '/usr/libexec/smartdesktopmimi/tuya-disabled')

    def test_firmware_rejects_old_programs_before_sdk_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            build, _ = self.fixture(root / 'product', 'public')
            sdk = root / 'sdk'
            (sdk / '.repo').mkdir(parents=True)
            config = sdk / '.config'
            config.write_bytes(b'preserved SDK input')
            env = dict(os.environ, AITVBOX_PRODUCT_BUILD=str(build.parent),
                       MIMI_PRODUCT_EDITION='public', AITVBOX_ENABLE_100ASK_CLOUD='OFF')
            result = subprocess.run(['bash', str(ROOT / 'scripts/build_firmware.sh'), str(sdk)],
                                    capture_output=True, text=True, env=env)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('PRODUCT EDITION BLOCKED', result.stderr)
            self.assertEqual(config.read_bytes(), b'preserved SDK input')
            self.assertEqual(sorted(p.name for p in sdk.iterdir()), ['.config', '.repo'])

    def test_real_program_package_install_public_private_and_mismatch(self):
        value = os.environ.get('MIMI_EDITION_PROGRAM_FIXTURES')
        if not value:
            self.skipTest('actual A133 edition programs not_run: set MIMI_EDITION_PROGRAM_FIXTURES')
        fixtures = Path(value).resolve()
        for edition in ('public', 'private'):
            with self.subTest(edition=edition), tempfile.TemporaryDirectory() as directory:
                root = Path(directory).resolve()
                product = root / 'product'
                shutil.copytree(fixtures / ('edition066-build-proof-' + edition) / 'build', product / 'build')
                (product / 'scripts').symlink_to(ROOT / 'scripts', target_is_directory=True)
                for relative in ('build/lv_port_linux/lvglsim', 'build/hdmi_preview/hdmi_preview', 'build/aitvbox-version',
                                 'build/aitvbox-tuya-pid', 'vendor/tuyaopen-a133-b6-libs/MNN/libMNN.so',
                                 'vendor/tuyaopen-a133-b6-libs/MNN/libMNN_Express.so',
                                 'vendor/tuyaopen-a133-b6-libs/models/mdtc_chunk_300ms.mnn',
                                 'vendor/tuyaopen-a133-b6-libs/models/tokens.txt',
                                 'apps/lv_port_linux/src/ui/font/SarasaUiSC-Regular.ttf',
                                 'apps/lv_port_linux/src/ui/font/SarasaUiSC-SemiBold.ttf', 'secrets/100ask_cloud.env',
                                 'secrets/100ask_ca.crt'):
                    path = product / relative;path.parent.mkdir(parents=True, exist_ok=True);path.write_text('fixture only')
                emotions = {'Neutral & Thinking': ['static', 'investigate', 'ponder', 'question'],
                            'Positive Emotions': ['smile'], 'Negative Emotions': ['sad', 'angry'],
                            'Surprise & Reactions': ['panic', 'shocked'], 'Physical & Special States': ['asleep']}
                for group, names in emotions.items():
                    for name in names:
                        path = product / 'third_party/aitvbox_emotion_lottie_pack' / group / (name + '.json')
                        path.parent.mkdir(parents=True, exist_ok=True);path.write_text('{}')
                key = root / 'public.pem';key.write_text('public verification key fixture')
                top = root / 'top';top.mkdir();inc = top / 'include';inc.mkdir()
                (top / 'rules.mk').write_text('INCLUDE_DIR:=' + str(inc) + '\nINSTALL_DIR:=mkdir -p\nINSTALL_BIN:=install -m755\nINSTALL_DATA:=install -m644\n')
                (inc / 'package.mk').touch()
                stage = root / 'rootfs'
                makefile = root / 'install.mk'
                makefile.write_text('TOPDIR:=' + str(top) + '\nAITVBOX_PRODUCT_BUILD:=' + str(product) +
                                    '\nMIMI_PRODUCT_EDITION:=' + edition + '\nAITVBOX_ENABLE_100ASK_CLOUD:=' + ('ON' if edition == 'private' else 'OFF') +
                                    '\nMIMI_SWU_PUBLIC_KEY:=' + str(key) + '\ninclude ' + str(ROOT / 'packaging/aitvbox-suite/Makefile') +
                                    '\nall:\n\t$(call Package/aitvbox-suite/install,' + str(stage) + ')\n')
                command = ['make', '--no-print-directory', '-f', str(makefile), 'all']
                result = subprocess.run(command, cwd=ROOT / 'packaging/aitvbox-suite', capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual((stage / 'etc/smartdesktopmimi/product-edition').read_text().strip(), edition)
                self.assertEqual((stage / 'usr/bin/your_chat_bot_QIO_1.0.1.bin').exists(), edition == 'private')
                self.assertEqual((stage / 'factory/tuya').exists(), edition == 'private')
                self.assertEqual((stage / 'usr/lib/libMNN.so').exists(), edition == 'private')
                self.assertFalse((stage / 'etc/100ask/cloud.conf').exists())
                self.assertFalse((stage / 'etc/100ask/ca.crt').exists())
                self.assertFalse((stage / 'factory/tuya/license.env').exists())
                # Installing both production packages must not claim the same file.
                # The factory tree here is a synthetic seal, not device/signature acceptance;
                # provider/runtime programs above are actual separately built A133 inputs.
                sys.path.insert(0, str(ROOT / 'tools/update'))
                import baseline
                (product / 'tools').symlink_to(ROOT / 'tools', target_is_directory=True)
                prepared = product / 'build/mimi-baseline'
                factory_root = prepared / 'rootfs'
                config = factory_root / 'etc/smartdesktopmimi';config.mkdir(parents=True)
                (config / 'device.json').write_text(json.dumps({'enrollment_id': ''}))
                (config / 'system-device.json').write_text(json.dumps({'product_edition': edition}))
                marker = config / 'product-edition';marker.write_text(edition + '\n');marker.chmod(0o444)
                factory = factory_root / 'usr/lib/smartdesktopmimi/factory';factory.mkdir(parents=True)
                (factory / 'manifest.json').write_text(json.dumps({'commit': 'a'*40, 'package_sha256': 'b'*64}))
                for entry in factory_root.rglob('*'):
                    entry.chmod(0o755 if entry.is_dir() else (0o444 if entry == marker else 0o644))
                baseline.write(prepared / 'baseline-lock.json', dict(schema_version=1, policy='MIMI-UPDATE-001/1.0.0',
                               factory_commit='a'*40, factory_version='1.0.0-test', factory_package_sha256='b'*64,
                               files=baseline.inventory(factory_root)))
                updater = product / 'build/mimi-update/mimi-update';updater.parent.mkdir()
                shutil.copy2('/bin/true', updater)
                with (top / 'rules.mk').open('a') as macros:macros.write('CP:=cp -fpR\n')
                sealed_suite = root / 'sealed-suite'
                suite_recipe = root / 'sealed-suite.mk'
                suite_recipe.write_text(makefile.read_text().replace(str(stage), str(sealed_suite)))
                result = subprocess.run(['make', '--no-print-directory', '-f', str(suite_recipe), 'all'],
                                        cwd=ROOT / 'packaging/aitvbox-suite', capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                updater_stage = root / 'updater-package';updater_recipe = root / 'updater.mk'
                updater_recipe.write_text('TOPDIR:=' + str(top) + '\nAITVBOX_PRODUCT_BUILD:=' + str(product) +
                                          '\ninclude ' + str(ROOT / 'packaging/aitvbox-mimi-update/Makefile') +
                                          '\nall:\n\t$(call Package/aitvbox-mimi-update/install,' + str(updater_stage) + ')\n')
                result = subprocess.run(['make', '--no-print-directory', '-f', str(updater_recipe), 'all'],
                                        cwd=ROOT / 'packaging/aitvbox-mimi-update', capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                payload = lambda tree: {p.relative_to(tree).as_posix() for p in tree.rglob('*') if p.is_file() or p.is_symlink()}
                self.assertEqual(payload(sealed_suite) & payload(updater_stage), set(), 'opkg package file ownership collision')
                self.assertFalse((sealed_suite / 'etc/smartdesktopmimi/product-edition').exists())
                final_marker = updater_stage / 'etc/smartdesktopmimi/product-edition'
                self.assertEqual(final_marker.read_bytes(), marker.read_bytes())
                self.assertEqual(final_marker.stat().st_mode & 0o777, 0o444)
                self.assertEqual(baseline.inventory(factory_root), json.loads((prepared / 'baseline-lock.json').read_text())['files'])
                # A corrupt/mismatched seal must fail, never recreate a second marker.
                marker.chmod(0o644);marker.write_text(('private' if edition == 'public' else 'public') + '\n');marker.chmod(0o444)
                invalid_stage = root / 'invalid-baseline-suite'
                invalid_recipe = root / 'invalid-baseline-suite.mk'
                invalid_recipe.write_text(makefile.read_text().replace(str(stage), str(invalid_stage)))
                result = subprocess.run(['make', '--no-print-directory', '-f', str(invalid_recipe), 'all'],
                                        cwd=ROOT / 'packaging/aitvbox-suite', capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('baseline has changed since preparation', result.stdout + result.stderr)
                self.assertFalse((invalid_stage / 'etc/smartdesktopmimi/product-edition').exists())
                # A mixed build must fail on the first install recipe line, before mkdir/copy.
                shutil.rmtree(stage)
                record = product / 'build/lv_port_linux/provider-build-result.json'
                original_record = record.read_bytes()
                body = json.loads(record.read_text());body['profile'] = 'private' if edition == 'public' else 'public'
                record.write_text(json.dumps(body))
                result = subprocess.run(command, cwd=ROOT / 'packaging/aitvbox-suite', capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('PRODUCT EDITION BLOCKED', result.stderr)
                self.assertFalse(stage.exists())
                record.write_bytes(original_record)
                # A structurally valid old lock cannot authorize different build receipts.
                component = fixtures/('edition067-'+edition)/'platform-product-lock.json'
                if not component.is_file():
                    self.fail('configured program fixtures also require the actual edition067 source lock')
                stale = json.loads(component.read_text())
                stale['programs']['provider_sha256'] = 'e'*64
                stale_lock = root/'stale-source-lock.json';stale_lock.write_text(json.dumps(stale))
                sdk = root/'sdk';(sdk/'.repo').mkdir(parents=True)
                sentinel = sdk/'.config';sentinel.write_bytes(b'unchanged SDK config')
                env = dict(os.environ, AITVBOX_PRODUCT_BUILD=str(product), MIMI_PRODUCT_EDITION=edition,
                           AITVBOX_ENABLE_100ASK_CLOUD='ON' if edition=='private' else 'OFF',
                           MIMI_PLATFORM_PRODUCT_LOCK=str(stale_lock))
                refused = subprocess.run(['bash',str(ROOT/'scripts/build_firmware.sh'),str(sdk)],capture_output=True,text=True,env=env)
                self.assertNotEqual(refused.returncode,0)
                self.assertIn('product lock program receipt differs',refused.stderr)
                self.assertEqual(sentinel.read_bytes(),b'unchanged SDK config')



if __name__ == '__main__':
    unittest.main()
