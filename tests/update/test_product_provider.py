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
        recipe = dict(cloud_profile=profile, tool_sha256=p.sha(tool),
                      sources={'provider.c': p.sha(code / 'provider.c')},
                      inputs={'sdk/include.h': p.sha(dependency)},
                      compiler_target=p.COMPILER, compiler_sha256=p.sha(compiler),
                      source_commit='fixture')
        rf = component / profile / 'provider-recipe.json'
        rf.write_text(json.dumps(recipe))
        return sdk, component, rf, {profile: p.sha(rf)}, p.sha(tool)

    def test_changed_source_dependency_tool_recipe_and_compiler_rejected(self):
        for target in ['source', 'dependency', 'tool', 'recipe', 'compiler']:
            with self.subTest(target=target), tempfile.TemporaryDirectory() as d:
                sdk, component, rf, pins, tool_hash = self.fixture(Path(d).resolve())
                files = dict(source=component / 'public/code/provider.c', dependency=sdk / 'include.h',
                             tool=component / 'build_provider.py', recipe=rf, compiler=sdk / p.COMPILER)
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
