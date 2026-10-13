#!/usr/bin/env python3
"""Replay a pinned private SDK snapshot without changing the source checkout.

This is a component audit, not a full SDK restore/build qualification. Reports
contain private input paths and belong in private build storage, never Releases.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import tarfile
import tempfile


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode()


def digest_stream(stream):
    h = hashlib.sha256()
    for block in iter(lambda: stream.read(1024 * 1024), b""):
        h.update(block)
    return h.hexdigest()


def digest(path):
    with Path(path).open("rb") as stream:
        return digest_stream(stream)


def unique_object(pairs):
    result = {}
    for name, value in pairs:
        if name in result:
            raise ValueError("duplicate JSON field: " + name)
        result[name] = value
    return result


def read_json(path):
    return json.loads(path.read_text(), object_pairs_hook=unique_object)


def relative(value):
    if not isinstance(value, str) or not value or "\\" in value or "\0" in value:
        raise ValueError("invalid relative path")
    p = PurePosixPath(value)
    if p.is_absolute() or str(p) != value or any(x in (".", "..", ".git") for x in p.parts):
        raise ValueError("unsafe relative path: " + value)
    return value


def regular_inside(root, name):
    name = relative(name)
    p = root
    for part in PurePosixPath(name).parts:
        p = p / part
        if p.is_symlink():
            raise ValueError("snapshot artifact symlink: " + name)
    if not p.is_file():
        raise ValueError("missing snapshot artifact: " + name)
    return p


def directory_inside(root, name):
    p = root
    for part in PurePosixPath(relative(name)).parts:
        p = p / part
        if p.is_symlink():
            raise ValueError("component directory symlink: " + name)
    if not p.is_dir():
        raise ValueError("missing component: " + name)
    return p


def sha_value(value):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-f]{64}", value):
        raise ValueError("invalid SHA256")
    return value


def git(repo, *args, env=None):
    # Inherited Git overrides must not redirect the observed checkout/index.
    clean = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    clean.update({"GIT_OPTIONAL_LOCKS": "0", "GIT_CONFIG_NOSYSTEM": "1"})
    if env:
        clean.update(env)
    return subprocess.check_output(
        ["git", "-C", str(repo), "-c", "core.hooksPath=/dev/null",
         "-c", "core.fsmonitor=false", *args], env=clean, stderr=subprocess.PIPE)


def names(raw):
    return [x.decode("utf-8") for x in raw.split(b"\0") if x]


def link_issue(name, target, entries):
    if not isinstance(target, str) or not target or "\0" in target:
        raise ValueError("invalid link target: " + name)
    if target.startswith("/"):
        return "absolute_link"
    parts = list(PurePosixPath(name).parent.parts)
    for part in PurePosixPath(target).parts:
        if part == "..":
            if not parts:
                return "link_outside_component"
            parts.pop()
        elif part != ".":
            parts.append(part)
    # A parent that is itself an archived link is never safe to extract through.
    for parent in PurePosixPath(name).parents:
        if str(parent) in entries and "link" in entries[str(parent)]:
            raise ValueError("entry below archived symlink: " + name)
    return None


def verify_archive(path, entries, repo):
    issues = []
    expected = set(entries)
    for name, data in entries.items():
        relative(name)
        if not isinstance(data, dict) or set(data) not in ({"sha256", "mode"}, {"link", "mode"}):
            raise ValueError("invalid archived input descriptor: " + name)
        if type(data["mode"]) is not int or not 0 <= data["mode"] <= 0o777:
            raise ValueError("unsafe input mode: " + name)
        for parent in PurePosixPath(name).parents:
            if str(parent) in entries:
                raise ValueError("file used as archive directory: " + name)
        if "link" in data:
            issue = link_issue(name, data["link"], entries)
            if issue:
                issues.append({"path": name, "reason": issue})
        else:
            sha_value(data["sha256"])
    seen = set()
    with tarfile.open(path, "r:gz") as archive:
        for member in archive:
            name = relative(member.name)
            if name in seen or name not in expected:
                raise ValueError("duplicate or unlisted archive member: " + name)
            seen.add(name)
            data = entries[name]
            if member.mode != data["mode"]:
                raise ValueError("archive mode mismatch: " + name)
            if "link" in data:
                if not member.issym() or member.linkname != data["link"]:
                    raise ValueError("archive symlink mismatch: " + name)
            else:
                if not member.isfile() or member.islnk():
                    raise ValueError("archive special entry: " + name)
                with archive.extractfile(member) as stream:
                    if digest_stream(stream) != data["sha256"]:
                        raise ValueError("archive hash mismatch: " + name)
            live = repo / name
            for parent in PurePosixPath(name).parents:
                if (repo / parent).is_symlink():
                    raise ValueError("live input symlink ancestor: " + name)
            mode = live.lstat().st_mode
            if stat.S_IMODE(mode) != data["mode"]:
                raise ValueError("live input mode mismatch: " + name)
            if "link" in data:
                if not stat.S_ISLNK(mode) or os.readlink(live) != data["link"]:
                    raise ValueError("live input symlink changed: " + name)
            elif not stat.S_ISREG(mode) or digest(live) != data["sha256"]:
                raise ValueError("live input changed: " + name)
    if seen != expected:
        raise ValueError("archive missing inputs")
    return issues


def verify(snapshot, sdk, expected_sha256):
    snapshot, sdk = Path(snapshot).resolve(), Path(sdk).resolve()
    index_path = regular_inside(snapshot, "sdk-components.json")
    expected_sha256 = sha_value(expected_sha256)
    if digest(index_path) != expected_sha256:
        raise ValueError("snapshot index external pin mismatch")
    index = read_json(index_path)
    if index.get("schema_version") != 1 or not index.get("components"):
        raise ValueError("invalid component index")
    component_paths, folders = set(), set()
    for item in index["components"]:
        path, folder = relative(item["path"]), relative(item["snapshot_directory"])
        if path in component_paths or folder in folders:
            raise ValueError("duplicate component or snapshot directory")
        if any(path.startswith(old + "/") or old.startswith(path + "/") for old in component_paths):
            raise ValueError("overlapping component paths")
        component_paths.add(path)
        folders.add(folder)
        if not re.fullmatch(r"[0-9a-f]{40}", item["commit"]):
            raise ValueError("component must pin a full commit")
    result = {"schema_version": 1, "kind": "sdk-component-replay-audit",
              "snapshot_sha256": expected_sha256, "components": [],
              "component_replay": "in_progress", "full_restore": "not_run",
              "system_build": "not_run", "release_qualified": False,
              "scope": "Private report; verifies Git replay and captured bytes, not ignored/root inputs."}
    for item in index["components"]:
        repo = directory_inside(sdk, item["path"])
        folder = directory_inside(snapshot, item["snapshot_directory"])
        inputs_file = regular_inside(folder, "inputs.json")
        inputs = read_json(inputs_file)
        if hashlib.sha256(canonical(inputs)).hexdigest() != sha_value(item["local_inputs_sha256"]):
            raise ValueError("local input manifest hash mismatch: " + item["path"])
        patch = regular_inside(folder, "tracked.patch")
        archive = regular_inside(folder, "untracked.tar.gz")
        if digest(patch) != sha_value(inputs["tracked_patch_sha256"]):
            raise ValueError("tracked patch hash mismatch: " + item["path"])
        if digest(archive) != sha_value(item["untracked_archive_sha256"]):
            raise ValueError("untracked archive hash mismatch: " + item["path"])
        if git(repo, "rev-parse", "HEAD").decode().strip() != item["commit"]:
            raise ValueError("live component commit changed: " + item["path"])
        before = git(repo, "diff", "--no-ext-diff", "--no-textconv", "--binary", "--full-index", "HEAD")
        if hashlib.sha256(before).hexdigest() != inputs["tracked_patch_sha256"]:
            raise ValueError("live tracked inputs changed: " + item["path"])
        untracked = git(repo, "ls-files", "--others", "--exclude-standard", "-z")
        if set(names(untracked)) != set(inputs["untracked"]):
            raise ValueError("live untracked input set changed: " + item["path"])
        issues = verify_archive(archive, inputs["untracked"], repo)
        # New index and object store: never write an index/tree into the SDK.
        with tempfile.TemporaryDirectory(prefix="mimi-sdk-replay-") as temporary:
            temp = Path(temporary)
            objects = temp / "objects"
            objects.mkdir()
            original_objects = Path(git(repo, "rev-parse", "--git-path", "objects").decode().strip())
            if not original_objects.is_absolute():
                original_objects = repo / original_objects
            env = {"GIT_INDEX_FILE": str(temp / "index"),
                   "GIT_OBJECT_DIRECTORY": str(objects),
                   "GIT_ALTERNATE_OBJECT_DIRECTORIES": str(original_objects.resolve())}
            git(repo, "read-tree", item["commit"], env=env)
            if patch.stat().st_size:
                git(repo, "apply", "--cached", "--binary", "--whitespace=nowarn", str(patch), env=env)
            tree = git(repo, "write-tree", env=env).decode().strip()
            changed = names(git(repo, "diff-tree", "--no-commit-id", "--name-only", "-r", "-z", item["commit"], tree, env=env))
        if before != git(repo, "diff", "--no-ext-diff", "--no-textconv", "--binary", "--full-index", "HEAD") or untracked != git(repo, "ls-files", "--others", "--exclude-standard", "-z"):
            raise ValueError("SDK changed during replay: " + item["path"])
        if git(repo, "rev-parse", "HEAD").decode().strip() != item["commit"]:
            raise ValueError("SDK commit changed during replay")
        verify_archive(archive, inputs["untracked"], repo)
        if hashlib.sha256(canonical(read_json(inputs_file))).hexdigest() != item["local_inputs_sha256"]:
            raise ValueError("snapshot input manifest changed during replay")
        if digest(patch) != inputs["tracked_patch_sha256"] or digest(archive) != item["untracked_archive_sha256"]:
            raise ValueError("snapshot changed during replay")
        ignored = names(git(repo, "ls-files", "--others", "--ignored", "--exclude-standard", "--directory", "-z"))
        result["components"].append({"path": item["path"], "commit": item["commit"],
            "restored_tracked_tree": tree, "changed_tracked_files": len(changed),
            "untracked_files_verified": len(inputs["untracked"]), "replay": "passed",
            "nonportable_inputs": issues, "ignored_paths_not_captured": ignored})
        print("replayed", len(result["components"]), len(index["components"]), item["path"], flush=True)
    if digest(index_path) != expected_sha256:
        raise ValueError("snapshot index changed during replay")
    result["component_replay"] = "passed"
    result["nonportable_input_count"] = sum(len(x["nonportable_inputs"]) for x in result["components"])
    result["ignored_path_count"] = sum(len(x["ignored_paths_not_captured"]) for x in result["components"])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot", required=True, type=Path)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists() or args.output.is_symlink():
        parser.error("output must be a new private report")
    report = verify(args.snapshot, args.sdk, args.expected_sha256)
    # Exclusive create prevents replacing earlier evidence; no SDK mutations.
    fd = os.open(str(args.output), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")


if __name__ == "__main__":
    main()
