import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class CommandTransactionTests(unittest.TestCase):
    def test_ordering_and_deadlines(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = pathlib.Path(tmp) / "transaction"
            subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                            "-I", str(ROOT), str(ROOT / "tests/architecture/fixtures/ai_command_transaction.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

if __name__ == "__main__":
    unittest.main()
