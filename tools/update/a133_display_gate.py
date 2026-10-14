#!/usr/bin/env python3
"""Check the fixed A133/B6 display contract against real build artifacts.

This is an offline build gate, not a device health or hardware acceptance test.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess
import tempfile

OPTION = "CONFIG_LCD_SUPPORT_D320FPC2403_MIPI"
PANEL = "d320fpc2403"
NODE = "/soc@03000000/lcd0@01c0c000"
DTB = "out/a133/b6/pack_out/sunxi.fex"
DEFCONFIG = "device/config/chips/a133/configs/b6/linux-4.9/openwrt_linux_defconfig"
OUTPUT = "out/a133/b6/openwrt"


class GateError(ValueError):
    pass


def input_file(root, relative):
    path = (root / relative).resolve()
    try:
        path.relative_to(root)
    except ValueError as exc:
        raise GateError("build input escapes selected SDK: " + relative) from exc
    if not path.is_file():
        raise GateError("missing build input: " + relative)
    return path


def enabled(text, relative):
    declarations = [line.strip() for line in text.splitlines()
                    if re.fullmatch(r"(?:" + OPTION + r"=.*|# " + OPTION + r" is not set)", line.strip())]
    if declarations != [OPTION + "=y"]:
        raise GateError(relative + ": selected panel requires exactly " + OPTION + "=y")


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1048576), b""):
            h.update(block)
    return h.hexdigest()


def command(argv):
    try:
        result = subprocess.run(argv, capture_output=True, text=True, timeout=30, check=True)
    except (OSError, subprocess.SubprocessError) as exc:
        raise GateError("artifact reader failed: " + Path(argv[0]).name) from exc
    return result.stdout


def verify(root, stage):
    root = root.resolve()
    if not (root / ".repo").is_dir():
        raise GateError("selected path is not a Tina repo workspace")
    values = {}
    for line in input_file(root, ".buildconfig").read_text().splitlines():
        if not line.startswith("export LICHEE_") or "=" not in line:
            continue
        key, value = line[len("export "):].split("=", 1)
        if key in ("LICHEE_IC", "LICHEE_BOARD", "LICHEE_ARCH", "LICHEE_KERN_VER"):
            if key in values:
                raise GateError("duplicate target declaration: " + key)
            parsed = shlex.split(value)
            values[key] = parsed[0] if len(parsed) == 1 else None
    expected = {"LICHEE_IC": "a133", "LICHEE_BOARD": "b6",
                "LICHEE_ARCH": "arm64", "LICHEE_KERN_VER": "linux-4.9"}
    if values != expected:
        raise GateError("unsupported SDK target for A133/B6 display gate")
    files = {}
    def checked(relative):
        path = input_file(root, relative)
        files[relative] = {"sha256": digest(path), "bytes": path.stat().st_size}
        return path
    enabled(checked(DEFCONFIG).read_text(), DEFCONFIG)
    result = {"schema": 1, "requirement": "REQ-MIMI-SDK-047", "status": "passed",
              "scope": "Offline A133/B6 selected panel build consistency only",
              "stage": stage, "panel": PANEL, "files": files,
              "physical_acceptance": "not_run", "release_qualified": False}
    if stage == "inputs":
        return result
    generated = "kernel/linux-4.9/output/.config"
    enabled(checked(generated).read_text(), generated)
    elf = checked("kernel/linux-4.9/vmlinux")
    with elf.open("rb") as stream:
        header = stream.read(20)
    if len(header) != 20 or header[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", header, 18)[0] != 183:
        raise GateError("built kernel is not little-endian ELF64 AArch64")
    nm = input_file(root, "out/toolchain/gcc-linaro-5.3.1-2016.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-nm")
    symbols = command([str(nm), "--defined-only", str(elf)])
    if not re.search(r"(?m)^\S+\s+[A-Za-z]\s+d320fpc2403_panel$", symbols):
        raise GateError("selected panel has no defined symbol in the built kernel")
    image = checked("kernel/linux-4.9/output/bImage")
    # Use the exact arm64/boot/Makefile conversion, so a new vmlinux cannot
    # validate a stale bImage and an equally stale packed boot image.
    objcopy = input_file(root, "out/toolchain/gcc-linaro-5.3.1-2016.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-objcopy")
    with tempfile.TemporaryDirectory(prefix="mimi-panel-gate-") as tmp:
        derived = Path(tmp) / "Image"
        command([str(objcopy), "-O", "binary", "-R", ".note", "-R", ".note.gnu.build-id",
                 "-R", ".comment", "-S", str(elf), str(derived)])
        if digest(derived) != files["kernel/linux-4.9/output/bImage"]["sha256"]:
            raise GateError("bImage differs from the checked kernel ELF")
    if stage == "kernel":
        return result
    # Pack transforms the source DTB. The boot-chain gate authenticates the
    # SDK extractor and verifies this exact DTB inside the final boot package.
    firmware = checked(OUTPUT + "/a133_linux_b6_uart0.img")
    bootchain = Path(__file__).resolve().parents[2] / "scripts/a133-bootchain-gate.sh"
    command(["bash", str(bootchain), str(firmware), str(root)])
    dtb = checked(DTB)
    for prop, value in {"lcd_used": "1", "lcd_driver_name": PANEL,
                        "lcd_x": "1024", "lcd_y": "768", "lcd_if": "4"}.items():
        actual = command(["fdtget", str(dtb), NODE, prop]).strip()
        if actual != value:
            raise GateError("effective DTB panel contract mismatch: " + prop)
    boot = checked(OUTPUT + "/boot.img")
    with boot.open("rb") as stream:
        header = stream.read(48)
        if len(header) < 48 or header[:8] != b"ANDROID!":
            raise GateError("unsupported boot image format")
        size = struct.unpack_from("<I", header, 8)[0]
        page = struct.unpack_from("<I", header, 36)[0]
        if page not in (2048, 4096, 8192, 16384) or size != image.stat().st_size or page + size > boot.stat().st_size:
            raise GateError("boot kernel bounds do not match the built image")
        stream.seek(page)
        h = hashlib.sha256()
        remaining = size
        while remaining:
            block = stream.read(min(1048576, remaining))
            if not block:
                raise GateError("truncated boot kernel")
            h.update(block)
            remaining -= len(block)
        if h.hexdigest() != files["kernel/linux-4.9/output/bImage"]["sha256"]:
            raise GateError("packed boot kernel differs from checked built kernel")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sdk", type=Path)
    parser.add_argument("--stage", choices=("inputs", "kernel", "packed"), required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        result = verify(args.sdk, args.stage)
    except (GateError, OSError, ValueError) as exc:
        result = {"schema": 1, "requirement": "REQ-MIMI-SDK-047", "status": "failed",
                  "stage": args.stage, "reason": str(exc), "release_qualified": False}
    text = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    print(text, end="")
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
