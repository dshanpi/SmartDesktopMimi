import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("a133_display_gate", ROOT / "tools/update/a133_display_gate.py")
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


class DisplayGateTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        (self.root / ".repo").mkdir()
        self.write(".buildconfig", "export LICHEE_IC=a133\nexport LICHEE_BOARD=b6\n"
                   "export LICHEE_ARCH=arm64\nexport LICHEE_KERN_VER=linux-4.9\n")
        self.write(gate.DEFCONFIG, gate.OPTION + "=y\n")

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content if isinstance(content, bytes) else content.encode())
        return path

    def reject(self, fragment, stage="inputs"):
        with self.assertRaisesRegex(gate.GateError, fragment):
            gate.verify(self.root, stage)

    def test_input_pass_is_explicitly_not_physical_acceptance(self):
        result = gate.verify(self.root, "inputs")
        self.assertEqual(result["status"], "passed")
        self.assertEqual(result["physical_acceptance"], "not_run")
        self.assertFalse(result["release_qualified"])

    def test_source_driver_disabled(self):
        self.write(gate.DEFCONFIG, "# " + gate.OPTION + " is not set\n")
        self.reject("requires exactly")

    def test_missing_driver_choice(self):
        self.write(gate.DEFCONFIG, "CONFIG_HIDRAW=y\n")
        self.reject("requires exactly")

    def test_modular_driver_does_not_meet_built_in_contract(self):
        self.write(gate.DEFCONFIG, gate.OPTION + "=m\n")
        self.reject("requires exactly")

    def test_conflicting_declarations_rejected(self):
        self.write(gate.DEFCONFIG, gate.OPTION + "=y\n# " + gate.OPTION + " is not set\n")
        self.reject("requires exactly")

    def test_other_board_rejected(self):
        p = self.root / ".buildconfig"
        p.write_text(p.read_text().replace("BOARD=b6", "BOARD=b10"))
        self.reject("unsupported SDK target")

    def test_duplicate_target_rejected(self):
        p = self.root / ".buildconfig"
        p.write_text(p.read_text() + "export LICHEE_IC=a133\n")
        self.reject("duplicate target")

    def test_active_build_ignoring_new_defconfig_rejected(self):
        self.write("kernel/linux-4.9/output/.config", "# " + gate.OPTION + " is not set\n")
        self.reject("output/.config", "kernel")

    def kernel_fixture(self):
        self.write("kernel/linux-4.9/output/.config", gate.OPTION + "=y\n")
        header = bytearray(20)
        header[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<H", header, 18, 183)
        self.write("kernel/linux-4.9/vmlinux", bytes(header))
        prefix = "out/toolchain/gcc-linaro-5.3.1-2016.05-x86_64_aarch64-linux-gnu/bin/"
        self.write(prefix + "aarch64-linux-gnu-nm", "fixture")
        self.write(prefix + "aarch64-linux-gnu-objcopy", "fixture")
        self.write("kernel/linux-4.9/output/bImage", b"old image")

    def test_wrong_architecture_kernel_rejected(self):
        self.kernel_fixture()
        p = self.root / "kernel/linux-4.9/vmlinux"
        header = bytearray(p.read_bytes())
        struct.pack_into("<H", header, 18, 62)
        p.write_bytes(header)
        self.reject("not little-endian ELF64 AArch64", "kernel")

    def test_declared_but_not_linked_driver_rejected(self):
        self.kernel_fixture()
        with patch.object(gate, "command", return_value="00000000 D default_panel\n"):
            self.reject("no defined symbol", "kernel")

    def test_new_elf_cannot_validate_stale_image(self):
        self.kernel_fixture()
        def reader(argv):
            if argv[0].endswith("-nm"):
                return "00000000 D d320fpc2403_panel\n"
            Path(argv[-1]).write_bytes(b"new image from ELF")
            return ""
        with patch.object(gate, "command", side_effect=reader):
            self.reject("bImage differs", "kernel")

    def packed_fixture(self):
        self.kernel_fixture()
        self.write(gate.DTB, b"reader fixture")
        self.write(gate.OUTPUT + "/a133_linux_b6_uart0.img", b"boot-chain fixture")
        header = bytearray(2048)
        header[:8] = b"ANDROID!"
        struct.pack_into("<I", header, 8, 9)
        struct.pack_into("<I", header, 36, 2048)
        boot = self.write(gate.OUTPUT + "/boot.img", bytes(header) + b"old image")
        values = {"lcd_used": "1", "lcd_driver_name": gate.PANEL,
                  "lcd_x": "1024", "lcd_y": "768", "lcd_if": "4"}
        def reader(argv):
            if argv[0].endswith("-nm"):
                return "00000000 D d320fpc2403_panel\n"
            if argv[0] == "fdtget":
                return values[argv[-1]] + "\n"
            if argv[0] == "bash":
                return "boot-chain fixture boundary accepted\n"
            Path(argv[-1]).write_bytes(b"old image")
            return ""
        return boot, values, reader

    def test_effective_dtb_other_panel_rejected(self):
        _, values, reader = self.packed_fixture()
        values["lcd_driver_name"] = "default_lcd"
        with patch.object(gate, "command", side_effect=reader):
            self.reject("lcd_driver_name", "packed")

    def test_effective_dtb_other_screen_rejected(self):
        _, values, reader = self.packed_fixture()
        values["lcd_x"] = "800"
        with patch.object(gate, "command", side_effect=reader):
            self.reject("lcd_x", "packed")

    def test_disabled_dtb_panel_rejected(self):
        _, values, reader = self.packed_fixture()
        values["lcd_used"] = "0"
        with patch.object(gate, "command", side_effect=reader):
            self.reject("lcd_used", "packed")

    def test_truncated_boot_rejected(self):
        boot, _, reader = self.packed_fixture()
        boot.write_bytes(boot.read_bytes()[:-1])
        with patch.object(gate, "command", side_effect=reader):
            self.reject("boot kernel bounds", "packed")

    def test_mismatched_packed_kernel_rejected(self):
        boot, _, reader = self.packed_fixture()
        boot.write_bytes(boot.read_bytes()[:-9] + b"bad image")
        with patch.object(gate, "command", side_effect=reader):
            self.reject("packed boot kernel differs", "packed")

    def test_fixture_matching_artifacts_still_do_not_pass_hardware(self):
        _, _, reader = self.packed_fixture()
        with patch.object(gate, "command", side_effect=reader):
            result = gate.verify(self.root, "packed")
        self.assertEqual(result["physical_acceptance"], "not_run")
        self.assertFalse(result["release_qualified"])


if __name__ == "__main__":
    unittest.main()
