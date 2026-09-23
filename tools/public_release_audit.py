#!/usr/bin/env python3
"""Fail-closed audit for a prospective public source snapshot.

The scanner reports paths and rule identifiers only; it never prints matched
secret material.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
PRIVATE_PATHS = (
    re.compile(r"^third_party/100ask-iot-sdk/.*"),
    re.compile(r"(^|/)factory-assistant-v1/(02-|03-|data/).*"),
    re.compile(r"(^|/)100ask_keys/.*"),
    re.compile(r"(^|/)vendor/tuyaopen-a133-b6-libs/.*"),
    re.compile(r"^third_party/TuyaOpen/\.tools/.*"),
    re.compile(r"(^|/)(target|node_modules|build|dist|out)(/|$).*"),
    re.compile(r".*\.(xlsx|sqlite|sqlite3|p12|pfx|jks)$", re.IGNORECASE),
    re.compile(r"(^|/)(id_rsa|id_ed25519)(\.|$)"),
    re.compile(r"^output/.*"),
    re.compile(r"(^|/)(playwright-report|test-results|coverage)(/|$).*"),
    re.compile(r"^docs/debug-evidence/.*\.(?:raw|raw\.gz|dump|log)$", re.IGNORECASE),
)
# Some upstream projects intentionally keep source code in a directory named
# `build`. Keep this exception narrow; third-party directories are never a
# blanket exception for archives, binaries, or large assets.
PUBLIC_SOURCE_PATHS = (
    re.compile(r"^third_party/TuyaOpen/src/libu8g2/u8g2/tools/font/build/.*"),
)
ARCHIVE_OR_RELEASE_IMAGE = re.compile(
    r".*\.(?:7z|img|iso|rar|tar|tar\.bz2|tar\.gz|tar\.xz|tgz|txz|zip)$",
    re.IGNORECASE,
)
UNCLASSIFIED_BINARY = re.compile(
    r".*\.(?:a|bin|dll|dylib|exe|iso|otf|pack|so(?:\.[0-9]+)*|ttf|wasm|woff2?)$",
    re.IGNORECASE,
)
PRIVATE_TEXT = (
    ("private-key", re.compile(
        rb"(?m)^-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----\r?\n"
        rb"(?:[A-Za-z0-9+/=]{16,}\r?\n){2,}"
    )),
    ("github-token", re.compile(rb"\bgh[pousr]_[A-Za-z0-9]{20,}\b")),
    ("openai-api-key", re.compile(rb"\bsk-[A-Za-z0-9_-]{20,}\b")),
    ("aws-access-key", re.compile(rb"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b")),
    ("tuya-device-credential", re.compile(
        rb"(?im)^\s*TUYA_OPENSDK_(?:UUID|AUTHKEY)\s*=\s*[A-Za-z0-9+/=_-]{16,}\s*$"
    )),
)
TEXT_SCAN_LIMIT = 4 * 1024 * 1024
MAX_UNCLASSIFIED_SIZE = 10 * 1024 * 1024
COMPONENT_INVENTORY = "third_party/components.json"
ASSET_INVENTORY = "docs/public-assets.json"
PUBLIC_RELEASE_PROFILE = "docs/public-release.json"
ASSET_SUFFIXES = {".jpeg", ".jpg", ".pdf", ".png", ".svg"}
ASSET_PREFIXES = (
    "docs/architecture-diagrams/rendered/",
    "docs/debug-evidence/",
    "docs/solution/assets/",
)
SOURCE_SUFFIXES = {
    ".c", ".cc", ".cmake", ".cpp", ".cxx", ".h", ".hh", ".hpp",
    ".json", ".md", ".py", ".rs", ".sh", ".ts", ".tsx", ".txt",
    ".yaml", ".yml",
}


def tracked_files(root: Path) -> list[Path]:
    result = subprocess.run(
        [
            "git", "ls-files", "--cached", "--others",
            "--exclude-standard", "-z",
        ],
        cwd=root,
        check=True,
        stdout=subprocess.PIPE,
    )
    return [root / item.decode("utf-8", "surrogateescape") for item in result.stdout.split(b"\0") if item]


def path_rule(relative: str) -> str | None:
    if any(pattern.search(relative) for pattern in PUBLIC_SOURCE_PATHS):
        return None
    if any(pattern.search(relative) for pattern in PRIVATE_PATHS):
        return "private-or-restricted-path"
    if ARCHIVE_OR_RELEASE_IMAGE.fullmatch(relative):
        return "archive-or-release-image"
    if UNCLASSIFIED_BINARY.fullmatch(relative):
        return "unclassified-binary"
    return None


def _safe_relative(value: object) -> str | None:
    if not isinstance(value, str) or not value:
        return None
    path = Path(value)
    if path.is_absolute() or ".." in path.parts or value != path.as_posix():
        return None
    return value.rstrip("/")


def _under(relative: str, prefix: str) -> bool:
    return relative == prefix or relative.startswith(prefix + "/")


def file_magic_rule(path: Path) -> str | None:
    with path.open("rb") as stream:
        return "unclassified-elf" if stream.read(4) == b"\x7fELF" else None


def release_profile_findings(
    root: Path, *, candidate: bool = False,
) -> tuple[list[dict[str, str]], tuple[str, ...], set[str]]:
    profile_path = root / PUBLIC_RELEASE_PROFILE
    try:
        document = json.loads(profile_path.read_text(encoding="utf-8"))
        exclusions = document["excluded_paths"]
        assets = document["redistributable_files"]
        if (
            document.get("schema_version") != 1
            or document.get("target") != "a133"
            or not isinstance(exclusions, list)
            or not isinstance(assets, list)
        ):
            raise ValueError("invalid schema")
    except (OSError, UnicodeError, json.JSONDecodeError, KeyError, ValueError):
        return ([{"path": PUBLIC_RELEASE_PROFILE, "rule": "invalid-public-release-profile"}], (), set())

    findings: list[dict[str, str]] = []
    excluded: list[str] = []
    seen_excluded: set[str] = set()
    for item in exclusions:
        relative = _safe_relative(item.get("path") if isinstance(item, dict) else None)
        reason = item.get("reason") if isinstance(item, dict) else None
        if relative is None or not isinstance(reason, str) or not reason or relative in seen_excluded:
            findings.append({"path": PUBLIC_RELEASE_PROFILE, "rule": "invalid-public-release-profile"})
            continue
        if any(_under(relative, parent) or _under(parent, relative) for parent in seen_excluded):
            findings.append({"path": relative, "rule": "overlapping-public-exclusion"})
            continue
        if any(pattern.search(relative) for pattern in PRIVATE_PATHS):
            findings.append({"path": relative, "rule": "invalid-public-exclusion"})
            continue
        seen_excluded.add(relative)
        excluded.append(relative)
        if not candidate and not (root / relative).exists() and not (root / relative).is_symlink():
            findings.append({"path": relative, "rule": "stale-public-exclusion"})

    classified: set[str] = set()
    for item in assets:
        if not isinstance(item, dict):
            findings.append({"path": PUBLIC_RELEASE_PROFILE, "rule": "invalid-public-release-profile"})
            continue
        relative = _safe_relative(item.get("path"))
        license_file = _safe_relative(item.get("license_file"))
        expected_hash = item.get("sha256")
        required = (item.get("component"), item.get("source"), item.get("license"))
        if (
            relative is None or license_file is None or relative in classified
            or not all(isinstance(value, str) and value for value in required)
            or not isinstance(expected_hash, str)
            or re.fullmatch(r"[0-9a-f]{64}", expected_hash) is None
        ):
            findings.append({"path": PUBLIC_RELEASE_PROFILE, "rule": "invalid-public-release-profile"})
            continue
        classified.add(relative)
        path = root / relative
        if not path.is_file() or path.is_symlink():
            findings.append({"path": relative, "rule": "redistributable-file-missing"})
        elif hashlib.sha256(path.read_bytes()).hexdigest() != expected_hash:
            findings.append({"path": relative, "rule": "redistributable-file-hash-mismatch"})
        if not (root / license_file).is_file():
            findings.append({"path": license_file, "rule": "redistributable-license-missing"})
    return findings, tuple(excluded), classified


def _is_documentation_asset(relative: str) -> bool:
    path = Path(relative)
    if path.suffix.lower() not in ASSET_SUFFIXES:
        return False
    if "/" not in relative:
        return True
    if relative == "docs/solution/smart-desktop-mimi-solution-brief.pdf":
        return True
    return relative.startswith(ASSET_PREFIXES)


def asset_findings(root: Path) -> tuple[list[dict[str, str]], set[str]]:
    inventory = root / ASSET_INVENTORY
    try:
        document = json.loads(inventory.read_text(encoding="utf-8"))
        assets = document["assets"]
        if document.get("schema_version") != 1 or not isinstance(assets, list):
            raise ValueError("invalid schema")
    except (OSError, UnicodeError, json.JSONDecodeError, KeyError, ValueError):
        return ([{"path": ASSET_INVENTORY, "rule": "invalid-asset-inventory"}], set())

    findings: list[dict[str, str]] = []
    classified: set[str] = set()
    for item in assets:
        if not isinstance(item, dict):
            findings.append({"path": ASSET_INVENTORY, "rule": "invalid-asset-inventory"})
            continue
        relative = item.get("path")
        source = item.get("source")
        license_expression = item.get("license")
        expected_hash = item.get("sha256")
        if not all(isinstance(value, str) and value for value in (
            relative, source, license_expression, expected_hash,
        )):
            findings.append({"path": ASSET_INVENTORY, "rule": "invalid-asset-inventory"})
            continue
        candidate = Path(relative)
        if candidate.is_absolute() or ".." in candidate.parts or relative in classified:
            findings.append({"path": ASSET_INVENTORY, "rule": "invalid-asset-inventory"})
            continue
        classified.add(relative)
        path = root / candidate
        if license_expression != "GPL-3.0-or-later":
            findings.append({"path": relative, "rule": "asset-license-not-approved"})
        if not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
            findings.append({"path": relative, "rule": "invalid-asset-hash"})
        elif not path.is_file() or path.is_symlink():
            findings.append({"path": relative, "rule": "asset-missing"})
        else:
            actual_hash = hashlib.sha256(path.read_bytes()).hexdigest()
            if actual_hash != expected_hash:
                findings.append({"path": relative, "rule": "asset-hash-mismatch"})
    return findings, classified


def component_findings(root: Path) -> list[dict[str, str]]:
    inventory = root / COMPONENT_INVENTORY
    try:
        document = json.loads(inventory.read_text(encoding="utf-8"))
        components = document["components"]
        if not isinstance(components, list):
            raise ValueError("components must be a list")
    except (OSError, UnicodeError, json.JSONDecodeError, KeyError, ValueError):
        return [{"path": COMPONENT_INVENTORY, "rule": "invalid-component-inventory"}]

    findings: list[dict[str, str]] = []
    for component in components:
        if not isinstance(component, dict):
            findings.append({
                "path": COMPONENT_INVENTORY,
                "rule": "invalid-component-inventory",
            })
            continue
        path = component.get("path")
        license_expression = component.get("license")
        distribution = component.get("distribution")
        source = component.get("source")
        revision = component.get("revision")
        license_file = component.get("license_file")
        if not all(isinstance(value, str) and value for value in (
            component.get("name"), path, source, revision, license_expression,
            license_file, distribution,
        )):
            findings.append({
                "path": COMPONENT_INVENTORY,
                "rule": "invalid-component-inventory",
            })
            continue
        if _safe_relative(path) is None or _safe_relative(license_file) is None:
            findings.append({
                "path": COMPONENT_INVENTORY,
                "rule": "invalid-component-inventory",
            })
            continue
        if (
            "ReviewRequired" in license_expression
            or distribution.startswith("blocked-")
        ):
            findings.append({"path": path, "rule": "license-review-required"})
        if not (root / license_file).is_file():
            findings.append({"path": license_file, "rule": "component-license-missing"})
    return findings


def audit(root: Path, *, candidate: bool = False) -> list[dict[str, str]]:
    findings: list[dict[str, str]] = component_findings(root)
    asset_issues, classified_assets = asset_findings(root)
    findings.extend(asset_issues)
    profile_issues, excluded_paths, redistributable_files = release_profile_findings(
        root, candidate=candidate,
    )
    findings.extend(profile_issues)
    for path in tracked_files(root):
        # `git ls-files` includes staged deletions. They are absent from the
        # candidate snapshot and therefore are not findings.
        if not path.exists() and not path.is_symlink():
            continue
        relative = path.relative_to(root).as_posix()
        if path.is_symlink():
            try:
                path.resolve(strict=True).relative_to(root.resolve())
            except (OSError, ValueError):
                findings.append({"path": relative, "rule": "symlink-outside-root"})
            continue
        if not path.is_file():
            continue
        restricted_rule = path_rule(relative)
        excluded = any(_under(relative, prefix) for prefix in excluded_paths)
        if excluded and candidate:
            findings.append({"path": relative, "rule": "excluded-public-path"})
            continue
        if excluded:
            continue
        if restricted_rule:
            if relative not in redistributable_files:
                findings.append({"path": relative, "rule": restricted_rule})
                continue
        if _is_documentation_asset(relative) and relative not in classified_assets:
            findings.append({"path": relative, "rule": "unclassified-public-asset"})
        if (
            path.stat().st_size > MAX_UNCLASSIFIED_SIZE
            and relative not in classified_assets
            and relative not in redistributable_files
            and path.suffix.lower() not in SOURCE_SUFFIXES
        ):
            findings.append({"path": relative, "rule": "unclassified-large-file"})
        try:
            magic_rule = file_magic_rule(path)
            if magic_rule and relative not in redistributable_files:
                findings.append({"path": relative, "rule": magic_rule})
            if path.stat().st_size <= TEXT_SCAN_LIMIT:
                data = path.read_bytes()
                for rule, pattern in PRIVATE_TEXT:
                    if pattern.search(data):
                        # Third-party cryptographic test vectors are not product secrets.
                        if relative.startswith("third_party/") and "/tests/" in relative:
                            continue
                        findings.append({"path": relative, "rule": rule})
        except OSError:
            findings.append({"path": relative, "rule": "unreadable"})
    unique = {(item["path"], item["rule"]): item for item in findings}
    return [unique[key] for key in sorted(unique)]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument(
        "--candidate", action="store_true",
        help="require every path marked for exclusion to be absent",
    )
    args = parser.parse_args()
    root = args.root.expanduser().resolve()
    if not (root / ".git").exists():
        parser.error(f"not a Git worktree: {root}")
    findings = audit(root, candidate=args.candidate)
    if args.json:
        print(json.dumps({"passed": not findings, "findings": findings}, indent=2))
    else:
        for finding in findings:
            print(f"BLOCK {finding['rule']}: {finding['path']}")
        print(f"public release audit: {'PASS' if not findings else 'BLOCKED'} ({len(findings)} findings)")
    return 0 if not findings else 3


if __name__ == "__main__":
    raise SystemExit(main())
