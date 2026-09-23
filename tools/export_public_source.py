#!/usr/bin/env python3
"""Create a history-free public candidate after all release gates pass."""

from __future__ import annotations

import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

import public_release_audit


ROOT = Path(__file__).resolve().parents[1]
LVGL_PATH = Path("apps/lv_port_linux/lvgl")


def git_output(*arguments: str, cwd: Path = ROOT) -> str:
    return subprocess.run(
        ["git", *arguments], cwd=cwd, check=True,
        stdout=subprocess.PIPE, text=True,
    ).stdout.strip()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def materialize_lvgl(destination: Path) -> str:
    fields = git_output("ls-tree", "HEAD", LVGL_PATH.as_posix()).split()
    if len(fields) < 3 or fields[1] != "commit":
        raise RuntimeError("LVGL is not recorded as a Git submodule in the source commit")
    expected_revision = fields[2]
    source = ROOT / LVGL_PATH
    actual_revision = git_output("rev-parse", "HEAD", cwd=source)
    if actual_revision != expected_revision:
        raise RuntimeError(
            f"LVGL checkout mismatch: expected {expected_revision}, found {actual_revision}"
        )
    target = destination / LVGL_PATH
    target.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="aitvbox-lvgl-export-") as temporary:
        archive = Path(temporary) / "lvgl.tar"
        with archive.open("wb") as output:
            subprocess.run(
                ["git", "archive", "--format=tar", expected_revision],
                cwd=source, stdout=output, check=True,
            )
        subprocess.run(["tar", "-xf", archive, "-C", target], check=True)
    provenance = {
        "schema_version": 1,
        "component": "LVGL",
        "source": "AI-DeskTop-Box-LVGL-v9.5",
        "revision": expected_revision,
        "license": "MIT",
    }
    (target / ".aitvbox-origin.json").write_text(
        json.dumps(provenance, indent=2) + "\n", encoding="utf-8",
    )
    return expected_revision


def update_component_inventory(destination: Path, lvgl_revision: str) -> None:
    path = destination / "third_party/components.json"
    document = json.loads(path.read_text(encoding="utf-8"))
    for component in document["components"]:
        if component.get("path") == LVGL_PATH.as_posix():
            component["distribution"] = "source-pruned-a133"
            component["revision"] = lvgl_revision
            break
    else:
        raise RuntimeError("LVGL is missing from third_party/components.json")
    path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


def load_release_profile(destination: Path) -> dict[str, object]:
    path = destination / public_release_audit.PUBLIC_RELEASE_PROFILE
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema_version") != 1 or document.get("target") != "a133":
        raise RuntimeError("invalid public release profile")
    return document


def apply_exclusions(destination: Path, profile: dict[str, object]) -> list[dict[str, object]]:
    report: list[dict[str, object]] = []
    for item in profile["excluded_paths"]:  # type: ignore[index]
        relative = public_release_audit._safe_relative(item.get("path"))
        if relative is None:
            raise RuntimeError("unsafe path in public release profile")
        target = destination / relative
        if not target.exists() and not target.is_symlink():
            raise RuntimeError(f"stale public exclusion: {relative}")
        files = [target] if target.is_file() or target.is_symlink() else [
            path for path in target.rglob("*") if path.is_file() or path.is_symlink()
        ]
        size = sum(path.lstat().st_size for path in files)
        report.append({
            "path": relative,
            "reason": item["reason"],
            "removed_files": len(files),
            "removed_bytes": size,
        })
        if target.is_dir() and not target.is_symlink():
            shutil.rmtree(target)
        else:
            target.unlink()
    return report


def write_size_report(destination: Path) -> None:
    files = [
        path for path in destination.rglob("*")
        if path.is_file() and ".git" not in path.relative_to(destination).parts
    ]
    groups: dict[str, dict[str, int]] = {}
    for path in files:
        relative = path.relative_to(destination)
        group = relative.parts[0]
        entry = groups.setdefault(group, {"files": 0, "bytes": 0})
        entry["files"] += 1
        entry["bytes"] += path.stat().st_size
    report = {
        "schema_version": 1,
        "files": len(files),
        "bytes": sum(path.stat().st_size for path in files),
        "top_level": dict(sorted(groups.items())),
    }
    (destination / "release/source-size.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8",
    )


def write_file_manifest(destination: Path) -> None:
    output = destination / "release/SHA256SUMS"
    files = sorted(
        path for path in destination.rglob("*")
        if path.is_file() and path != output and ".git" not in path.relative_to(destination).parts
    )
    lines = [f"{sha256(path)}  {path.relative_to(destination).as_posix()}" for path in files]
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_reproducible_archive(destination: Path, archive: Path, epoch: int) -> None:
    paths = git_output("ls-files", "-z", cwd=destination).split("\0")
    paths = sorted(relative for relative in paths if relative)
    with archive.open("wb") as raw_output:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw_output, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.PAX_FORMAT) as tar:
                root_info = tarfile.TarInfo("AI-DeskTopBox")
                root_info.type = tarfile.DIRTYPE
                root_info.mode = 0o755
                root_info.mtime = epoch
                tar.addfile(root_info)
                for relative in paths:
                    path = destination / relative
                    info = tar.gettarinfo(path, arcname=f"AI-DeskTopBox/{relative}")
                    info.uid = 0
                    info.gid = 0
                    info.uname = "root"
                    info.gname = "root"
                    info.mtime = epoch
                    if info.isfile():
                        with path.open("rb") as source:
                            tar.addfile(info, source)
                    else:
                        tar.addfile(info)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("destination", type=Path)
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--acknowledge-rotated-secrets", action="store_true")
    parser.add_argument("--acknowledge-licenses-reviewed", action="store_true")
    args = parser.parse_args()
    destination = args.destination.expanduser().resolve()
    archive = (
        args.archive.expanduser().resolve()
        if args.archive else destination.with_name(destination.name + ".tar.gz")
    )
    checksum = archive.with_name(archive.name + ".sha256")
    if ROOT == destination or ROOT in destination.parents:
        print("error: destination must be outside the development repository", file=sys.stderr)
        return 2
    if archive == destination or ROOT == archive or ROOT in archive.parents:
        print("error: archive must be outside the development repository", file=sys.stderr)
        return 2
    if not args.acknowledge_rotated_secrets or not args.acknowledge_licenses_reviewed:
        print("error: secret rotation and license review acknowledgements are required", file=sys.stderr)
        return 2
    status = subprocess.run(
        ["git", "status", "--porcelain"], cwd=ROOT, check=True,
        stdout=subprocess.PIPE, text=True
    ).stdout
    if status:
        print("error: commit the audited candidate before export", file=sys.stderr)
        return 2
    findings = public_release_audit.audit(ROOT)
    if findings:
        print(f"error: public release audit has {len(findings)} blocking findings", file=sys.stderr)
        return 3
    if destination.exists() and any(destination.iterdir()):
        print(f"error: destination is not empty: {destination}", file=sys.stderr)
        return 2
    if archive.exists() or checksum.exists():
        print(f"error: archive output already exists: {archive} or {checksum}", file=sys.stderr)
        return 2

    source_revision = git_output("rev-parse", "HEAD")
    source_epoch = int(git_output("show", "-s", "--format=%ct", "HEAD"))
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="aitvbox-public-export-") as temporary:
        source_archive = Path(temporary) / "source.tar"
        with source_archive.open("wb") as output:
            subprocess.run(["git", "archive", "--format=tar", "HEAD"], cwd=ROOT, stdout=output, check=True)
        subprocess.run(["tar", "-xf", source_archive, "-C", destination], check=True)
    gitmodules = destination / ".gitmodules"
    if gitmodules.exists():
        gitmodules.unlink()
    lvgl_revision = materialize_lvgl(destination)
    update_component_inventory(destination, lvgl_revision)
    profile = load_release_profile(destination)
    excluded = apply_exclusions(destination, profile)

    release = destination / "release"
    release.mkdir()
    (release / "excluded-content.json").write_text(
        json.dumps({
            "schema_version": 1,
            "target": profile["target"],
            "entries": excluded,
            "removed_files": sum(item["removed_files"] for item in excluded),
            "removed_bytes": sum(item["removed_bytes"] for item in excluded),
        }, indent=2) + "\n",
        encoding="utf-8",
    )
    provenance = {
        "schema_version": 1,
        "source_revision": source_revision,
        "source_date_epoch": source_epoch,
        "lvgl_revision": lvgl_revision,
        "history": "fresh-single-commit",
        "release_profile": public_release_audit.PUBLIC_RELEASE_PROFILE,
    }
    (release / "source-provenance.json").write_text(
        json.dumps(provenance, indent=2) + "\n", encoding="utf-8",
    )
    environment = os.environ.copy()
    environment["SOURCE_REVISION"] = source_revision
    environment["SOURCE_DATE_EPOCH"] = str(source_epoch)
    subprocess.run(
        [sys.executable, destination / "tools/generate_sbom.py", "--output", release],
        cwd=destination, env=environment, check=True,
    )
    write_size_report(destination)
    write_file_manifest(destination)

    subprocess.run(["git", "init"], cwd=destination, check=True)
    subprocess.run(
        ["git", "symbolic-ref", "HEAD", "refs/heads/main"],
        cwd=destination, check=True,
    )
    subprocess.run(["git", "config", "user.name", "AI-DeskTopBox Public Release"], cwd=destination, check=True)
    subprocess.run(["git", "config", "user.email", "opensource@09make.com"], cwd=destination, check=True)
    # The source archive and materialized LVGL revision have already passed the
    # release-profile audit.  Force-add that complete tree so generic ignore
    # patterns (for example ``config.h``) cannot silently drop tracked files
    # from a nested upstream component when the history-free repo is created.
    subprocess.run(["git", "add", "-f", "-A"], cwd=destination, check=True)
    candidate_findings = public_release_audit.audit(destination, candidate=True)
    if candidate_findings:
        for finding in candidate_findings:
            print(f"BLOCK {finding['rule']}: {finding['path']}", file=sys.stderr)
        print(
            f"error: exported candidate audit has {len(candidate_findings)} blocking findings",
            file=sys.stderr,
        )
        return 3
    commit_environment = environment.copy()
    commit_environment["GIT_AUTHOR_DATE"] = f"@{source_epoch} +0000"
    commit_environment["GIT_COMMITTER_DATE"] = f"@{source_epoch} +0000"
    subprocess.run(
        ["git", "commit", "-m", "Initial public source release"],
        cwd=destination, env=commit_environment, check=True,
    )
    write_reproducible_archive(destination, archive, source_epoch)
    checksum.write_text(f"{sha256(archive)}  {archive.name}\n", encoding="utf-8")
    print(f"public candidate created: {destination}")
    print(f"source archive: {archive}")
    print(f"archive checksum: {checksum}")
    print(
        "excluded from public source: "
        f"{sum(item['removed_files'] for item in excluded)} files, "
        f"{sum(item['removed_bytes'] for item in excluded)} bytes"
    )
    print("Run the clean-clone build and tests before adding a remote or publishing.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
