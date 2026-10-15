"""Prevent platform services and changed legacy adapters entering app builds."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/update'))
import source_ownership as ownership


class SourceOwnershipTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.prefix = 'apps/lv_port_linux/src/'
        self.files = {}
        for name, owner in [('system/service_wifi.c', 'platform'),
                            ('system/backlight_control.c', 'platform-client-frozen'),
                            ('ipc/ipc_socket.c', 'shared-frozen'),
                            ('system/app_manager.c', 'application')]:
            full = self.prefix + name
            file = self.root / full
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text('int fixture;\n')
            self.files[full] = {'owner': owner}
            if owner.endswith('-frozen'):
                self.files[full]['sha256'] = ownership.digest(file)
        record = dict(schema_version=1, platform_id='a133',
                      scopes=[self.prefix+'system', self.prefix+'ipc'], files=self.files)
        lock = self.root / ownership.LOCK
        lock.parent.mkdir(parents=True)
        lock.write_text(json.dumps(record))

    def commands(self, name):
        return [{'directory': str(self.root), 'file': self.prefix + name}]

    def test_frozen_adapter_and_application_are_accepted(self):
        for name in ['system/backlight_control.c', 'system/app_manager.c', 'ipc/ipc_socket.c']:
            result = ownership.audit_commands(self.root, self.commands(name))
            self.assertEqual(result['compiled_source_count'], 1)

    def test_platform_service_outside_backend_directory_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'platform service entered'):
            ownership.audit_commands(self.root, self.commands('system/service_wifi.c'))

    def test_new_system_file_requires_ownership_review(self):
        (self.root / self.prefix / 'system/new_driver.c').write_text('int unreviewed;')
        with self.assertRaisesRegex(ValueError, 'unclassified boundary'):
            ownership.verify(self.root)

    def test_changed_shared_or_platform_adapter_requires_new_baseline(self):
        for name in ['system/backlight_control.c', 'ipc/ipc_socket.c']:
            path = self.root / self.prefix / name
            old = path.read_bytes()
            path.write_text('int changed;')
            with self.assertRaisesRegex(ValueError, 'frozen platform/shared'):
                ownership.verify(self.root)
            path.write_bytes(old)

    def test_source_alias_and_external_file_cannot_bypass_owner(self):
        path = self.root / self.prefix / 'ui/alias.c'
        path.parent.mkdir()
        path.symlink_to(self.root / self.prefix / 'system/service_wifi.c')
        with self.assertRaisesRegex(ValueError, 'untrusted compilation'):
            ownership.audit_commands(self.root, self.commands('ui/alias.c'))
        with self.assertRaisesRegex(ValueError, 'untrusted compilation'):
            ownership.audit_commands(self.root, [{'file': '/outside/main.c'}])

    def test_retired_platform_source_rejected_even_when_not_compiled(self):
        lock = self.root / ownership.LOCK
        policy = json.loads(lock.read_text())
        name = self.prefix + 'system/old_driver.c'
        policy['retired_platform_sources'] = {name: {'sha256': '0' * 64}}
        lock.write_text(json.dumps(policy))
        ownership.verify(self.root)
        (self.root / name).write_text('int old_driver;')
        with self.assertRaisesRegex(ValueError, 'retired platform implementation'):
            ownership.audit_commands(self.root, self.commands('system/app_manager.c'))

    def test_empty_inventory_and_unknown_subtree_rejected(self):
        with self.assertRaisesRegex(ValueError, 'empty compilation'):
            ownership.audit_commands(self.root, [])
        path = self.root / self.prefix / 'unknown/source.c'
        path.parent.mkdir(); path.write_text('int other;')
        with self.assertRaisesRegex(ValueError, 'unclassified compilation'):
            ownership.audit_commands(self.root, self.commands('unknown/source.c'))


if __name__ == '__main__':
    unittest.main()
