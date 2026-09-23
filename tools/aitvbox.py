#!/usr/bin/env python3
"""One entrypoint for AITVBox platform builds and validation.

This tool intentionally depends only on the Python standard library so it can
run before a vendor SDK has been initialized.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
PLATFORMS = ROOT / "platforms"
SUPPORTED_COMMANDS = ("doctor", "configure", "build", "test", "package", "flash")


class CommandError(RuntimeError):
    pass


def load_platform(platform_id: str) -> dict[str, Any]:
    path = PLATFORMS / platform_id / "platform.json"
    if not path.is_file():
        available = ", ".join(sorted(p.parent.name for p in PLATFORMS.glob("*/platform.json")))
        raise CommandError(f"unknown platform {platform_id!r}; available: {available}")
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise CommandError(f"invalid platform manifest {path}: {exc}") from exc
    required = {
        "schema_version", "id", "soc", "arch", "support", "hardware", "sdk", "toolchain",
        "capabilities", "components", "packaging", "flash", "validation",
    }
    missing = sorted(required.difference(data))
    if missing:
        raise CommandError(f"platform manifest is missing: {', '.join(missing)}")
    if data["schema_version"] != 1 or data["id"] != platform_id:
        raise CommandError("platform manifest schema or id mismatch")
    return data


def sdk_root(manifest: dict[str, Any], override: str | None) -> Path | None:
    sdk = manifest["sdk"]
    value = override or os.environ.get(sdk["environment"]) or sdk["default_root"]
    return Path(value).expanduser().resolve() if value else None


def inspect(manifest: dict[str, Any], root: Path | None) -> list[str]:
    errors: list[str] = []
    if manifest["support"] == "skeleton":
        errors.append(
            f"{manifest['id']} is an adapter skeleton, not a validated platform"
        )
    if root is None:
        errors.append(f"SDK root not set; use --sdk-root or {manifest['sdk']['environment']}")
    elif not root.is_dir():
        errors.append(f"SDK root does not exist: {root}")
    else:
        for marker in manifest["sdk"]["markers"]:
            if not (root / marker).exists():
                errors.append(f"SDK marker missing: {root / marker}")
    for command in ("git", "make", "cmake", "python3"):
        if shutil.which(command) is None:
            errors.append(f"required host command missing: {command}")
    for relative in (manifest["toolchain"], manifest["validation"]):
        if not (ROOT / relative).is_file():
            errors.append(f"repository input missing: {relative}")
    return errors


def run(
    command: list[str], *, cwd: Path = ROOT, dry_run: bool = False,
    env: dict[str, str] | None = None,
) -> None:
    print("+", " ".join(command))
    if not dry_run:
        subprocess.run(command, cwd=cwd, check=True, env=env)


def require_ready(manifest: dict[str, Any], root: Path | None) -> None:
    errors = inspect(manifest, root)
    if errors:
        raise CommandError("\n".join(f"- {error}" for error in errors))


def command_doctor(args: argparse.Namespace, manifest: dict[str, Any], root: Path | None) -> None:
    errors = inspect(manifest, root)
    print(f"platform: {manifest['id']} ({manifest['soc']}, {manifest['arch']})")
    print(f"support: {manifest['support']}")
    print(f"sdk: {root or 'not configured'}")
    print("hardware:")
    for name, value in manifest["hardware"].items():
        print(f"  {name}: {value}")
    print("capabilities:", ", ".join(manifest["capabilities"]) or "none declared")
    if errors:
        raise CommandError("\n".join(f"- {error}" for error in errors))
    print("doctor: PASS")


def command_configure(args: argparse.Namespace, manifest: dict[str, Any], root: Path | None) -> None:
    require_ready(manifest, root)
    output = ROOT / "out" / manifest["id"] / args.config
    output.mkdir(parents=True, exist_ok=True)
    resolved = {
        "schema_version": 1,
        "platform": manifest["id"],
        "configuration": args.config,
        "sdk_root": str(root),
        "toolchain": str((ROOT / manifest["toolchain"]).resolve()),
        "capabilities": manifest["capabilities"],
    }
    destination = output / "configuration.json"
    destination.write_text(json.dumps(resolved, indent=2) + "\n", encoding="utf-8")
    print(destination.relative_to(ROOT))


def require_a133(manifest: dict[str, Any]) -> None:
    if manifest["id"] != "a133":
        raise CommandError(
            f"{manifest['id']} has no production build adapter; complete its port checklist first"
        )


def command_build(args: argparse.Namespace, manifest: dict[str, Any], root: Path | None) -> None:
    require_ready(manifest, root)
    require_a133(manifest)
    command = [str(ROOT / "scripts/build_apps.sh")]
    if args.clean:
        command.append("--clean")
    command.append(str(root))
    run(command, dry_run=args.dry_run)


def command_test(args: argparse.Namespace, manifest: dict[str, Any], root: Path | None) -> None:
    if args.scope in ("architecture", "all", "public"):
        run([sys.executable, "-m", "unittest", "tests.architecture.test_architecture"])
        run([sys.executable, "scripts/check_architecture.py"])
    if args.scope in ("host", "all", "public"):
        require_a133(manifest)
        environment = os.environ.copy()
        if args.scope != "public":
            require_ready(manifest, root)
            environment["AITVBOX_TINA_SDK"] = str(root)
        else:
            environment.pop("AITVBOX_TINA_SDK", None)
        print("+", sys.executable, "-m unittest tests.platform.test_host_integration")
        subprocess.run(
            [sys.executable, "-m", "unittest", "tests.platform.test_host_integration"],
            cwd=ROOT,
            env=environment,
            check=True,
        )
    if args.scope == "public":
        ui = ROOT / "apps/ipkvm/upstream/ui"
        npm = shutil.which("npm")
        if npm is None:
            raise CommandError("npm is required for the public validation scope")
        run([npm, "ci"], cwd=ui)
        run([npm, "run", "build:device"], cwd=ui)
        run([npm, "run", "lint:public"], cwd=ui)
    if args.scope in ("go", "all", "public"):
        require_a133(manifest)
        go = shutil.which("go") or "/usr/local/go/bin/go"
        if not Path(go).is_file():
            raise CommandError("Go toolchain missing from PATH and /usr/local/go/bin/go")
        run([go, "test", "./..."], cwd=ROOT / "apps/ipkvm/upstream")
        run([go, "test", "-race", "./..."], cwd=ROOT / "apps/ipkvm/upstream")
    if args.scope == "public":
        audit_command = [sys.executable, "tools/public_release_audit.py"]
        if not (ROOT / ".gitmodules").exists():
            audit_command.append("--candidate")
        run(audit_command)
        with tempfile.TemporaryDirectory(prefix="aitvbox-sbom-a-") as first_dir, \
             tempfile.TemporaryDirectory(prefix="aitvbox-sbom-b-") as second_dir:
            environment = os.environ.copy()
            environment.setdefault("SOURCE_DATE_EPOCH", "1700000000")
            for output in (first_dir, second_dir):
                run(
                    [sys.executable, "tools/generate_sbom.py", "--output", output],
                    env=environment,
                )
            for name in ("aitvbox.spdx.json", "aitvbox.cdx.json"):
                if (Path(first_dir) / name).read_bytes() != (Path(second_dir) / name).read_bytes():
                    raise CommandError(f"SBOM generation is not deterministic: {name}")
    print(f"test ({args.scope}): PASS")


def command_package(args: argparse.Namespace, manifest: dict[str, Any], root: Path | None) -> None:
    require_ready(manifest, root)
    require_a133(manifest)
    command = [str(ROOT / manifest["packaging"]["entrypoint"])]
    if args.allow_dirty:
        command.append("--allow-dirty")
    if args.check_only:
        command.append("--check-only")
    command.append(str(root))
    run(command, dry_run=args.dry_run)


def command_flash(args: argparse.Namespace, manifest: dict[str, Any], root: Path | None) -> None:
    require_ready(manifest, root)
    require_a133(manifest)
    image = Path(args.image).expanduser().resolve()
    if not image.is_file():
        raise CommandError(f"firmware image not found: {image}")
    gate = ROOT / manifest["flash"]["gate"]
    run([str(gate), str(image), str(root)], dry_run=args.dry_run)
    if not args.dry_run:
        print("flash gate: PASS")
        print("Use PhoenixSuit/LiveSuit on the designated test board, then run the board checklist.")
        print("Automatic flashing is deliberately disabled because the vendor flow is operator-assisted.")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(prog="aitvbox", description=__doc__)
    subparsers = result.add_subparsers(dest="command", required=True)
    for name in SUPPORTED_COMMANDS:
        item = subparsers.add_parser(name)
        item.add_argument("--platform", required=True, choices=("a133", "a527", "v883", "rk3576", "rv1106"))
        item.add_argument("--sdk-root")
        if name == "configure":
            item.add_argument("--config", default="release", choices=("debug", "release"))
        elif name == "build":
            item.add_argument("--clean", action="store_true")
            item.add_argument("--dry-run", action="store_true")
        elif name == "test":
            item.add_argument(
                "--scope", default="all",
                choices=("architecture", "host", "go", "public", "all"),
            )
        elif name == "package":
            item.add_argument("--allow-dirty", action="store_true")
            item.add_argument("--check-only", action="store_true")
            item.add_argument("--dry-run", action="store_true")
        elif name == "flash":
            item.add_argument("--image", required=True)
            item.add_argument("--dry-run", action="store_true")
    return result


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        manifest = load_platform(args.platform)
        root = sdk_root(manifest, args.sdk_root)
        globals()[f"command_{args.command}"](args, manifest, root)
    except (CommandError, subprocess.CalledProcessError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return exc.returncode if isinstance(exc, subprocess.CalledProcessError) else 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
