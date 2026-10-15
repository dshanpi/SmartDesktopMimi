"""Reject original-source fallback and malformed provider build inputs."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('provider_split', ROOT / 'tools/update/platform_provider.py')
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)


class ProviderSplitTests(unittest.TestCase):
    def test_dependency_parser_preserves_escaped_paths_and_continuations(self):
        self.assertEqual(p.dependencies(b'mimi_deps: /source/a.c \\\n /sdk/with\\ space.h\n'), [Path('/source/a.c'), Path('/sdk/with space.h')])
        with self.assertRaises(ValueError):
            p.dependencies(b'other: /source/a.c')

    def test_shell_response_and_plugin_inputs_rejected(self):
        for token in ['a;reboot', '`cmd`', '@file', '-fplugin=/tmp/p.so', '&&', '-wrapper', 'a\nb']:
            with self.subTest(token=token), self.assertRaises(ValueError):
                p.check_argv(['gcc', token])
        p.check_argv(['gcc', '-O3', '-c', '/source/main.c'])

    def test_preprocessor_removes_only_compilation_outputs(self):
        self.assertEqual(p.remove_output(['gcc', '--sysroot=/sdk', '-O3', '-o', 'old.o', '-c', '/src/main.c']), ['--sysroot=/sdk', '-O3', '/src/main.c'])
        with self.assertRaises(ValueError):
            p.remove_output(['gcc', '-o'])

    def test_dependency_boundaries_alias_and_actual_contents(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d).resolve()
            source, sdk, private, origin = [root / n for n in ['component', 'sdk', 'private', 'origin']]
            for x in [source, sdk, private, origin]:
                x.mkdir()
                (x / 'file.h').write_text(x.name)
            expected = {'project/file.h': p.sha(source / 'file.h'), 'sdk/file.h': p.sha(sdk / 'file.h'), 'private/file.h': p.sha(private / 'file.h')}
            self.assertEqual(p.audit_dependencies([source / 'file.h', sdk / 'file.h', private / 'file.h'], source, sdk, private, origin), expected)
            with self.assertRaisesRegex(ValueError, 'original application'):
                p.audit_dependencies([origin / 'file.h'], source, sdk, private, origin)
            (root / 'outside.h').write_text('outside')
            with self.assertRaisesRegex(ValueError, 'unclassified'):
                p.audit_dependencies([root / 'outside.h'], source, sdk, private)
            (source / 'alias.h').symlink_to(private / 'file.h')
            with self.assertRaisesRegex(ValueError, 'noncanonical'):
                p.audit_dependencies([source / 'alias.h'], source, sdk, private)
            (sdk / 'alias.h').symlink_to(sdk / 'file.h')
            self.assertEqual(p.audit_dependencies([sdk / 'alias.h'], source, sdk, private), {'sdk/file.h': expected['sdk/file.h']})
            (sdk / 'escape.h').symlink_to(root / 'outside.h')
            with self.assertRaisesRegex(ValueError, 'unclassified'):
                p.audit_dependencies([sdk / 'escape.h'], source, sdk, private)

    def test_relocation_and_missing_placeholder(self):
        roots = {'SDK_ROOT': Path('/sdk'), 'SOURCE_ROOT': Path('/old/project')}
        arg = p.normalize('--sysroot=/sdk/target', roots)
        self.assertEqual(p.expand(arg, dict(SDK_ROOT=Path('/new-sdk'))), '--sysroot=/new-sdk/target')
        with self.assertRaises(ValueError):
            p.expand('${UNKNOWN}/file', roots)


if __name__ == '__main__':
    unittest.main()
