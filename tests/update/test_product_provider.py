"""Product source boundary failures must precede any destructive build work."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('product_provider', ROOT / 'scripts/build_a133_provider.py')
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)


class ProductProviderTests(unittest.TestCase):
    def fixture(self, root, profile='public'):
        sdk = root / 'sdk'
        (sdk / '.repo').mkdir(parents=True)
        component = sdk / p.PREFIX
        code = component / profile / 'code'
        code.mkdir(parents=True)
        (code / 'provider.c').write_text('/* source */')
        tool = component / 'build_provider.py'
        tool.write_text('# fixture only')
        compiler = sdk / p.COMPILER
        compiler.parent.mkdir(parents=True)
        compiler.write_text('compiler')
        dependency = sdk / 'include.h'
        dependency.write_text('fixed input')
        ai = component / p.EDITION / 'code' / p.AI_SOURCE
        ai.parent.mkdir(parents=True)
        ai.write_text('/* shared runtime includes selected reader */')
        reader = component / p.EDITION / 'public/mimi_tuya_license_provider.h'
        reader.parent.mkdir(parents=True)
        reader.write_text('/* public always disabled */')
        recipe = dict(cloud_profile=profile, tool_sha256=p.sha(tool),
                      sources={'provider.c': p.sha(code / 'provider.c'), p.AI_SOURCE: p.sha(ai), p.READER_SOURCE: p.sha(reader)},
                      inputs={'sdk/include.h': p.sha(dependency)},
                      compiler_target=p.COMPILER, compiler_sha256=p.sha(compiler),
                      source_commit='fixture', translation_units=[], link_flags=[],
                      edition_source_version=2, edition_source_requirement='REQ-MIMI-PRIVATE-066',
                      reference_state='audited_new_source_reference', reference_backend_sha256='a' * 64,
                      prior_backend_sha256='b' * 64, original_backend_sha256='b' * 64)
        recipe['source_graph_sha256'] = p.source_graph(recipe)
        rf = component / p.EDITION / profile / 'provider-recipe.json'
        rf.parent.mkdir(parents=True, exist_ok=True)
        rf.write_text(json.dumps(recipe))
        return sdk, component, rf, {profile: p.sha(rf)}, p.sha(tool)

    def private_fixture(self, root):
        sdk, component, rf, pins, tool_hash = self.fixture(root, 'private')
        external, product = root / 'external-private-sdk', root / 'private-product'
        external.mkdir()
        product.mkdir()
        recipe = json.loads(rf.read_text())
        files = {}
        for name, relative in p.PRIVATE_SOURCES.items():
            source = product / relative
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text('/* product pairing source */')
            source.chmod(0o644)
            recipe['sources'][name] = p.sha(source)
            historical = component / 'private/code' / name
            historical.parent.mkdir(parents=True, exist_ok=True)
            historical.write_text('/* obsolete SDK copy, must never be read */')
            files[relative] = dict(sha256=p.sha(source), bytes=source.stat().st_size, git_mode='100644')
        tuya_files = {}
        for relative in (p.TUYA_READER, 'adapters/tuya/mimi_device_pairing.c'):
            source = product / relative
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_text('/* private Tuya source */')
            source.chmod(0o644)
            tuya_files[relative] = dict(sha256=p.sha(source), bytes=source.stat().st_size, git_mode='100644')
        tuya_lock = product / p.TUYA_LOCK
        tuya_lock.write_text(json.dumps(dict(schema=1, component='tuya-device-pairing', api_version=1, files=tuya_files)))
        recipe['sources'][p.READER_SOURCE] = p.sha(product / p.TUYA_READER)
        recipe['source_graph_sha256'] = p.source_graph(recipe)
        rf.write_text(json.dumps(recipe))
        pins['private'] = p.sha(rf)
        lock = dict(schema=1, component='100ask-auto-pairing', composition=p.PRIVATE_SOURCES,
                    files=files, provider_recipe_sha256=pins['private'], source_commit='a' * 40)
        lock_file = product / p.PRIVATE_LOCK
        lock_file.parent.mkdir(parents=True, exist_ok=True)
        lock_file.write_text(json.dumps(lock))
        return sdk, component, rf, pins, tool_hash, external, product, lock_file

    def test_private_requires_product_without_historical_fallback(self):
        with tempfile.TemporaryDirectory() as d:
            sdk, component, rf, pins, tool_hash, external, product, lock = self.private_fixture(Path(d).resolve())
            with patch.object(p, 'TOOL_HASH', tool_hash), patch.object(p, 'RECIPES', pins), patch.object(p, 'PRIVATE_LOCK_HASH', p.sha(lock)), patch.object(p, 'TUYA_LOCK_HASH', p.sha(product / p.TUYA_LOCK)):
                with self.assertRaisesRegex(ValueError, 'explicit private product'):
                    p.preflight(str(sdk), 'private', str(external))
                result = p.preflight(str(sdk), 'private', str(external), str(product))
                composed = p.compose(component, result[3], result[6], Path(d) / 'composition')
                for name, relative in p.PRIVATE_SOURCES.items():
                    self.assertEqual((composed / 'code' / name).read_bytes(), (product / relative).read_bytes())
                    (component / 'private/code' / name).unlink()
                # Even absent old SDK copies cannot trigger a fallback or failure.
                p.preflight(str(sdk), 'private', str(external), str(product))

    def test_private_product_changes_and_aliases_rejected(self):
        for target in ('source', 'lock', 'mode', 'source-alias', 'lock-alias'):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as d:
                sdk, component, rf, pins, tool_hash, external, product, lock = self.private_fixture(Path(d).resolve())
                with patch.object(p, 'TOOL_HASH', tool_hash), patch.object(p, 'RECIPES', pins), patch.object(p, 'PRIVATE_LOCK_HASH', p.sha(lock)), patch.object(p, 'TUYA_LOCK_HASH', p.sha(product / p.TUYA_LOCK)):
                    source = product / next(iter(p.PRIVATE_SOURCES.values()))
                    p.preflight(str(sdk), 'private', str(external), str(product))
                    if target in ('source', 'lock'):
                        (source if target == 'source' else lock).write_text('changed')
                    elif target == 'mode':
                        source.chmod(0o755)
                    else:
                        selected = source if target == 'source-alias' else lock
                        other = Path(d) / 'alias-target'
                        selected.rename(other)
                        selected.symlink_to(other)
                    with self.assertRaises(ValueError):
                        p.preflight(str(sdk), 'private', str(external), str(product))

    def test_public_rejects_private_flags_before_reading_inputs(self):
        for private, product in (('/missing-private', None), (None, '/missing-product')):
            with self.assertRaisesRegex(ValueError, 'public profile forbids'):
                p.preflight('/missing-sdk', 'public', private, product)

    def test_private_wrong_composition_recipe_and_overlapping_root_rejected(self):
        for change in ('recipe', 'composition', 'files'):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as d:
                sdk, component, rf, pins, tool_hash, external, product, lock = self.private_fixture(Path(d).resolve())
                body = json.loads(lock.read_text())
                body[{'recipe': 'provider_recipe_sha256', 'composition': 'composition', 'files': 'files'}[change]] = {} if change != 'recipe' else 'bad'
                lock.write_text(json.dumps(body))
                with patch.object(p, 'TOOL_HASH', tool_hash), patch.object(p, 'RECIPES', pins), patch.object(p, 'PRIVATE_LOCK_HASH', p.sha(lock)), patch.object(p, 'TUYA_LOCK_HASH', p.sha(product / p.TUYA_LOCK)):
                    with self.assertRaisesRegex(ValueError, 'composition or provider recipe'):
                        p.preflight(str(sdk), 'private', str(external), str(product))
                    with self.assertRaisesRegex(ValueError, 'separate from SDK'):
                        p.preflight(str(sdk), 'private', str(external), str(external))

    def test_existing_output_preserved_before_compiler_execution(self):
        with tempfile.TemporaryDirectory() as d:
            sdk, component, rf, pins, tool_hash, external, product, lock = self.private_fixture(Path(d).resolve())
            output = Path(d) / 'old-build'
            output.mkdir()
            old = output / 'lv_backend'
            old.write_bytes(b'preserve existing candidate')
            args = argparse.Namespace(sdk=str(sdk), profile='private', private_sdk=str(external),
                                      private_product_root=str(product), output=str(output), check=False)
            with patch.object(p, 'TOOL_HASH', tool_hash), patch.object(p, 'RECIPES', pins), patch.object(p, 'PRIVATE_LOCK_HASH', p.sha(lock)), patch.object(p, 'TUYA_LOCK_HASH', p.sha(product / p.TUYA_LOCK)), patch.object(p.subprocess, 'run') as execute:
                with self.assertRaisesRegex(ValueError, 'new directory'):
                    p.build(args)
                execute.assert_not_called()
            self.assertEqual(old.read_bytes(), b'preserve existing candidate')

    def test_changed_source_dependency_tool_recipe_and_compiler_rejected(self):
        for target in ['source', 'dependency', 'tool', 'recipe', 'compiler', 'ai', 'reader']:
            with self.subTest(target=target), tempfile.TemporaryDirectory() as d:
                sdk, component, rf, pins, tool_hash = self.fixture(Path(d).resolve())
                files = dict(source=component / 'public/code/provider.c', dependency=sdk / 'include.h',
                             tool=component / 'build_provider.py', recipe=rf, compiler=sdk / p.COMPILER,
                             ai=component / p.EDITION / 'code' / p.AI_SOURCE,
                             reader=component / p.EDITION / 'public/mimi_tuya_license_provider.h')
                with patch.object(p, 'TOOL_HASH', tool_hash), patch.object(p, 'RECIPES', pins):
                    p.preflight(str(sdk), 'public')
                    files[target].write_text('changed')
                    with self.assertRaises(ValueError):
                        p.preflight(str(sdk), 'public')

    def test_profile_and_private_boundary_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            sdk, component, rf, pins, tool_hash = self.fixture(Path(d).resolve(), 'private')
            with patch.object(p, 'TOOL_HASH', tool_hash), patch.object(p, 'RECIPES', pins):
                with self.assertRaisesRegex(ValueError, 'external private SDK'):
                    p.preflight(str(sdk), 'private')
                with self.assertRaisesRegex(ValueError, 'external private SDK'):
                    p.preflight(str(sdk), 'private', str(sdk))
                with self.assertRaises(ValueError):
                    p.preflight(str(sdk), 'public')

    def test_unapproved_reference_and_source_graph_rejected(self):
        for change in ('reference_state', 'source_graph_sha256', 'edition_source_version', 'reference_backend_sha256'):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as d:
                sdk, component, rf, pins, tool_hash = self.fixture(Path(d).resolve())
                recipe = json.loads(rf.read_text())
                recipe[change] = 'unapproved'
                rf.write_text(json.dumps(recipe))
                pins['public'] = p.sha(rf)
                with patch.object(p, 'TOOL_HASH', tool_hash), patch.object(p, 'RECIPES', pins):
                    with self.assertRaisesRegex(ValueError, 'edition/source graph/reference'):
                        p.preflight(str(sdk), 'public')

    def test_only_exact_new_reference_is_accepted_with_prior_kept(self):
        for altered in (False, True):
            with self.subTest(altered=altered), tempfile.TemporaryDirectory() as d:
                root = Path(d).resolve()
                sdk, component, rf, pins, tool_hash = self.fixture(root)
                payload = b'new public program without private reader'
                import hashlib
                expected = hashlib.sha256(payload).hexdigest()
                recipe = json.loads(rf.read_text())
                recipe['reference_backend_sha256'] = expected
                rf.write_text(json.dumps(recipe))
                pins['public'] = p.sha(rf)
                output = root / 'new-build'
                args = argparse.Namespace(sdk=str(sdk), profile='public', private_sdk=None,
                                          private_product_root=None, output=str(output), check=False)

                def compile_probe(*_args, **_kwargs):
                    output.mkdir()
                    (output / 'lv_backend').write_bytes(payload + (b'changed' if altered else b''))
                    result = dict(status='passed', cloud_profile='public', recipe_sha256=pins['public'],
                                  no_original_source_dependency=True, backend_sha256=p.sha(output / 'lv_backend'),
                                  original_backend_sha256=recipe['prior_backend_sha256'], byte_identical=False,
                                  translation_units=30)
                    (output / 'build-result.json').write_text(json.dumps(result))

                with patch.object(p, 'TOOL_HASH', tool_hash), patch.object(p, 'RECIPES', pins), patch.object(p.subprocess, 'run', compile_probe):
                    if altered:
                        with self.assertRaisesRegex(ValueError, 'build result mismatch'):
                            p.build(args)
                        self.assertFalse((output / 'product-provider-result.json').exists())
                    else:
                        receipt = p.build(args)
                        self.assertEqual(receipt['backend_sha256'], expected)
                        self.assertEqual(receipt['prior_backend_sha256'], 'b' * 64)
                        self.assertFalse(receipt['byte_identical_to_prior'])

    def test_private_tuya_reader_tamper_cannot_fall_back_to_sdk(self):
        for target in ('reader', 'runtime', 'lock'):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as d:
                sdk, component, rf, pins, tool_hash, external, product, lock = self.private_fixture(Path(d).resolve())
                with patch.object(p, 'TOOL_HASH', tool_hash), patch.object(p, 'RECIPES', pins), patch.object(p, 'PRIVATE_LOCK_HASH', p.sha(lock)), patch.object(p, 'TUYA_LOCK_HASH', p.sha(product / p.TUYA_LOCK)):
                    p.preflight(str(sdk), 'private', str(external), str(product))
                    path = {'reader': p.TUYA_READER, 'runtime': 'adapters/tuya/mimi_device_pairing.c', 'lock': p.TUYA_LOCK}[target]
                    (product / path).write_text('tampered')
                    with self.assertRaises(ValueError):
                        p.preflight(str(sdk), 'private', str(external), str(product))

    def test_source_alias_rejected_even_with_identical_body(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d).resolve()
            sdk, component, rf, pins, tool_hash = self.fixture(root)
            source = component / 'public/code/provider.c'
            other = root / 'outside.c'
            source.rename(other)
            source.symlink_to(other)
            with patch.object(p, 'TOOL_HASH', tool_hash), patch.object(p, 'RECIPES', pins):
                with self.assertRaises(ValueError):
                    p.preflight(str(sdk), 'public')

    def test_missing_sdk_component_preserves_existing_build_outputs(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d).resolve()
            product, sdk = root / 'product', root / 'sdk'
            for rel in ['scripts', 'build/lv_port_linux', 'third_party/TuyaOpen',
                        'vendor/tuyaopen-a133-b6-libs', 'apps/lv_port_linux',
                        'apps/hdmi_preview', 'apps/platform_services', 'apps/ipkvm']:
                (product / rel).mkdir(parents=True)
            for name in ['build_apps.sh', 'lib.sh', 'build_a133_provider.py']:
                shutil.copyfile(ROOT / 'scripts' / name, product / 'scripts' / name)
            (product / 'scripts/audit_english_ui.py').write_text('pass\n')
            (product / 'apps/lv_port_linux/build_openwrt.sh').touch()
            for app in ['hdmi_preview', 'platform_services', 'ipkvm']:
                (product / 'apps' / app / 'Makefile').write_text('clean:\n\tfalse\n')
            (sdk / '.repo').mkdir(parents=True)
            (sdk / 'build').mkdir()
            (sdk / 'build/envsetup.sh').touch()
            tc = sdk / 'out/toolchain/gcc-linaro-5.3.1-2016.05-x86_64_aarch64-linux-gnu/bin'
            tc.mkdir(parents=True)
            for name in ['gcc', 'g++', 'ar', 'ld']:
                f = tc / ('aarch64-linux-gnu-' + name)
                f.write_text('#!/bin/sh\nexit 99\n')
                f.chmod(0o755)
            old = product / 'build/lv_port_linux/lv_backend'
            old.write_bytes(b'existing valid artifact')
            hostbin = root / 'hostbin'
            hostbin.mkdir()
            for name in ['node', 'npm']:
                tool = hostbin / name
                tool.write_text('#!/bin/sh\nexit 99\n')
                tool.chmod(0o755)
            env = dict(os.environ, AITVBOX_ENABLE_100ASK_CLOUD='OFF', AITVBOX_100ASK_SDK_ROOT='',
                       PATH=str(hostbin) + os.pathsep + os.environ['PATH'])
            result = subprocess.run(['bash', str(product / 'scripts/build_apps.sh'), '--clean', str(sdk)],
                                    capture_output=True, text=True, env=env)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('A133 SDK PROVIDER BLOCKED', result.stderr)
            self.assertEqual(old.read_bytes(), b'existing valid artifact')
            self.assertFalse(list(root.glob('product/build/provider-*')))

    def test_missing_npm_rejected_before_source_audit_or_output_clean(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d).resolve()
            product, sdk, hostbin = [root / n for n in ['product', 'sdk', 'hostbin']]
            (product / 'scripts').mkdir(parents=True)
            (product / 'build/lv_port_linux').mkdir(parents=True)
            (sdk / '.repo').mkdir(parents=True)
            hostbin.mkdir()
            for name in ['build_apps.sh', 'lib.sh']:
                shutil.copyfile(ROOT / 'scripts' / name, product / 'scripts' / name)
            for name in ['bash', 'dirname', 'git', 'make', 'cmake', 'python3']:
                (hostbin / name).symlink_to(shutil.which(name))
            node = hostbin / 'node'
            node.write_text('#!/bin/sh\nexit 99\n')
            node.chmod(0o755)
            old = product / 'build/lv_port_linux/lv_backend'
            old.write_bytes(b'existing valid artifact')
            env = dict(os.environ, AITVBOX_ENABLE_100ASK_CLOUD='OFF', PATH=str(hostbin))
            result = subprocess.run(['/bin/bash', str(product / 'scripts/build_apps.sh'), '--clean', str(sdk)],
                                    capture_output=True, text=True, env=env)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('missing command: npm', result.stderr)
            self.assertEqual(old.read_bytes(), b'existing valid artifact')
            self.assertFalse((product / 'build/hdmi_preview').exists())


if __name__ == '__main__':
    unittest.main()
