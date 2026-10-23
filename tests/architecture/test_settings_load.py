"""Real loader corruption/migration cases in isolated files, not device recovery."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "apps/lv_port_linux/src/system"
FIXTURES = Path(__file__).with_name("fixtures")

class SettingsLoadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)
        for kind in ("migration", "fault", "path", "original_faults"):
            command = ["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
                       "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-no-pie",
                       "-I" + str(SOURCE), str(FIXTURES / ("settings_load_" + kind + ".c"))]
            if kind == "fault":
                command += ["-Wl,--wrap=fopen", "-Wl,--wrap=fseek", "-Wl,--wrap=fread"]
            if kind == "original_faults":
                command += [str(SOURCE / "settings.c")]
            command += ["-o", str(cls.root / kind)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)

    def run_case(self, kind, *args):
        result = subprocess.run([str(self.root / kind), *map(str, args)],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_supported_migrations(self):
        for version in range(9):
            with self.subTest(version=version):
                self.run_case("migration", version, self.root / "migration-record")

    def test_failed_reads_and_malformed_records_are_preserved(self):
        cases = ["open", "seek", "read", "short", "extra", "empty", "replace"]
        cases += ["bool" + str(i) for i in range(6)]
        for case in cases:
            with self.subTest(case=case):
                self.run_case("fault", case, self.root / "fault-record")

    def test_original_failure_regressions(self):
        for case in ("future", "brightness", "bool"):
            with self.subTest(case=case):
                self.run_case("original_faults", case, self.root / "original-record")

    def test_long_path_never_accesses_truncated_alias(self):
        parent = self.root / ("a" * 100) / ("b" * 100)
        parent.mkdir(parents=True)
        target = parent / ("c" * 60)
        alias = Path(str(target)[:255])
        self.assertNotEqual(target, alias)
        record = self.root / "valid-record"
        self.run_case("migration", 8, record)
        data = record.read_bytes()
        alias.write_bytes(data)
        target.write_bytes(data)
        self.run_case("path", target)
        self.assertEqual(alias.read_bytes(), data)
        self.assertEqual(target.read_bytes(), data)
        self.assertFalse(Path(str(alias) + ".lock").exists())

if __name__ == "__main__":
    unittest.main()
