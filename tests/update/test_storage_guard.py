"""Execute the production preinit hook with read-only host probe fixtures."""
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
GUARD = ROOT / 'packaging/aitvbox-mimi-update/files/78_mimi_storage_guard'
HARNESS = r'''
INITRAMFS=0
HOOK=
boot_hook_add() { HOOK="$2"; }
readlink() {
    case "$1" in
        /dev/by-name/rootfs_data)
            [ "$SCENARIO" = missing ] && return 1
            [ "$SCENARIO" = wrong ] && { echo /dev/mmcblk0p4; return; }
            echo /dev/mmcblk0p7 ;;
        /dev/by-name/UDISK) echo /dev/mmcblk0p9 ;;
        *) return 1 ;;
    esac
}
fw_printenv() {
    case "$SCENARIO" in
        pending) echo rootfs_data:UDISK; return 0 ;;
        failed_with_output) echo UDISK; return 1 ;;
        *) return 1 ;;
    esac
}
block() {
    case "$SCENARIO" in
        probe_failed) return 1 ;;
        failed_with_ext4) printf '%s: TYPE="ext4"\n' "$2"; return 1 ;;
        empty) return 0 ;;
        foreign) printf '%s: TYPE="vfat"\n' "$2" ;;
        wrong_probe_device) echo '/dev/mmcblk0p4: TYPE="ext4"' ;;
        *) printf '%s: UUID="fixture" VERSION="1.0" TYPE="ext4"\n' "$2" ;;
    esac
}
'''


class StorageGuardTests(unittest.TestCase):
    def call(self, scenario, body):
        script = (HARNESS + '\n. "$1"\n' +
                  'mimi_storage_is_block() { [ "$SCENARIO" != nonblock ]; }\n' + body)
        return subprocess.run(['/bin/sh', '-c', script, 'test', str(GUARD)],
                              env=dict(os.environ, SCENARIO=scenario), text=True,
                              capture_output=True, timeout=5)

    def test_existing_ext4_allows_registered_boot_hook(self):
        p = self.call('valid', '"$HOOK"; echo NEXT_VENDOR_HOOK')
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(p.stdout.strip(), 'NEXT_VENDOR_HOOK')

    def test_all_failure_paths_stop_before_vendor_formatter(self):
        for scenario in ('missing', 'wrong', 'nonblock', 'pending',
                         'failed_with_output', 'probe_failed', 'failed_with_ext4',
                         'empty', 'foreign', 'wrong_probe_device'):
            with self.subTest(scenario=scenario):
                p = self.call(scenario, '''
                    mimi_storage_recovery_shell() { echo RECOVERY; exit 74; }
                    "$HOOK"
                    echo NEXT_VENDOR_HOOK
                ''')
                self.assertEqual(p.returncode, 74, p.stdout + p.stderr)
                self.assertIn('RECOVERY', p.stdout)
                self.assertNotIn('NEXT_VENDOR_HOOK', p.stdout)

    def test_exiting_recovery_shell_rechecks_instead_of_bypassing(self):
        p = self.call('valid', '''
            checks=0
            mimi_storage_preflight() {
                checks=$((checks+1))
                [ "$checks" -ge 3 ]
            }
            mimi_storage_recovery_shell() { echo RECOVERY; }
            sleep() { :; }
            "$HOOK"
            echo CHECKS=$checks
            echo NEXT_VENDOR_HOOK
        ''')
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(p.stdout.count('\nRECOVERY\n'), 2)
        self.assertIn('CHECKS=3', p.stdout)
        self.assertTrue(p.stdout.rstrip().endswith('NEXT_VENDOR_HOOK'))

    def test_successful_read_only_preflight_does_not_enter_recovery(self):
        p = self.call('valid', '''
            mimi_storage_recovery_shell() { echo BAD_RECOVERY; exit 99; }
            mimi_storage_preflight
        ''')
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertNotIn('BAD_RECOVERY', p.stdout)

    def test_initramfs_hook_is_protected_as_well(self):
        script = 'INITRAMFS=1; boot_hook_add() { echo "$1:$2"; }; . "$1"'
        p = subprocess.run(['/bin/sh', '-c', script, 'test', str(GUARD)],
                           text=True, capture_output=True, check=True)
        self.assertEqual(p.stdout.strip(), 'initramfs:mimi_storage_guard')


if __name__ == '__main__':
    unittest.main()
