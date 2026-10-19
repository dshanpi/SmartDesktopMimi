"""Metadata/control-flow regression; mocked checker is not signature evidence."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import test_release_gate as fixture
gate = fixture.gate

class ApplicationRuntimeContractTests(unittest.TestCase):
    def fixture(self, root):
        lock, report = fixture.ReleaseEvidenceTests().fixture(root)
        manifest = gate.load(root/'manifest.json')
        manifest['targets'][0]['identity']['ipc_contract'] = 'mimi-ipc-v1.0'
        device = gate.load(root/'device.json')
        device['identity']['ipc_contract'] = 'mimi-ipc-v1.0'
        for name, value in [('manifest.json', manifest), ('device.json', device)]:
            (root/name).write_text(json.dumps(value))
            lock['assets'][name] = gate.sha(root/name)
        (root/'release-lock.json').write_text(json.dumps(lock))
        return lock

    def test_declared_runtime_differs_from_unchanged_build_contract(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(gate.subprocess, 'run') as verify:
            root = Path(tmp); self.fixture(root)
            result = gate.check(root, 'draft', Path('/checker'), Path('/trust'))
            self.assertEqual(result['ipc_contract'], 'mimi-ipc-v1.1')
            self.assertEqual(gate.load(root/'device.json')['identity']['ipc_contract'], 'mimi-ipc-v1.0')
            self.assertTrue(any(c.args[0][1] == 'verify' and c.kwargs.get('check') is True for c in verify.call_args_list))
            with self.assertRaisesRegex(ValueError, 'required acceptance'):
                gate.check(root, 'stable', Path('/checker'), Path('/trust'))

    def test_build_contract_still_exact(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(gate.subprocess, 'run') as verify:
            root = Path(tmp); lock = self.fixture(root)
            lock['ipc_contract'] = 'mimi-ipc-v9.9'
            (root/'release-lock.json').write_text(json.dumps(lock))
            with self.assertRaisesRegex(ValueError, 'release/interface contract mismatch'):
                gate.check(root, 'draft', Path('/checker'), Path('/trust'))
            verify.assert_not_called()

    def test_undeclared_device_cannot_inherit_source_contract(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(gate.subprocess, 'run'):
            root = Path(tmp); lock = self.fixture(root)
            device = gate.load(root/'device.json')
            device['identity']['ipc_contract'] = lock['ipc_contract']
            (root/'device.json').write_text(json.dumps(device))
            lock['assets']['device.json'] = gate.sha(root/'device.json')
            (root/'release-lock.json').write_text(json.dumps(lock))
            with self.assertRaisesRegex(ValueError, 'undeclared target'):
                gate.check(root, 'draft', Path('/checker'), Path('/trust'))

if __name__ == '__main__': unittest.main()
