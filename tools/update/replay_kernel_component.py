"""Reconstruct the saved Tina kernel component without its compiler cache.

Preparation never changes the selected SDK. Activation is separately guarded
by a terminal build receipt, live-process checks, and exact source readback.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import time

import sdk_snapshot as audit

COMPONENT = 'kernel/linux-4.9'
RECEIPT = '.mimi-kernel-replay.json'
GPU_CONFIG = 'modules/gpu/img-rgx/linux/rogue_km/binary_sunxi_linux_nullws_release/config_kernel.mk'


def relocated_gpu_config(archive, inputs, sdk):
    """Accept only two generated absolute paths, derived from saved bytes."""
    if GPU_CONFIG not in inputs:
        return {}
    with tarfile.open(archive, 'r:gz') as stream:
        member = stream.getmember(GPU_CONFIG)
        if not member.isfile() or member.size > 1024 * 1024:
            raise ValueError('saved GPU configuration type or size changed')
        data = stream.extractfile(member).read()
    if hashlib.sha256(data).hexdigest() != inputs[GPU_CONFIG]['sha256']:
        raise ValueError('saved GPU configuration bytes changed')
    text = data.decode('utf-8')
    lines = text.splitlines(keepends=True)
    kernel_lines = [line for line in lines if line.startswith('override KERNELDIR := ')]
    cross_lines = [line for line in lines if line.startswith('override KERNEL_CROSS_COMPILE := ')]
    if len(kernel_lines) != 1 or len(cross_lines) != 1:
        raise ValueError('saved GPU path fields are ambiguous')
    old_kernel = kernel_lines[0][len('override KERNELDIR := '):].rstrip('\r\n')
    old_cross = cross_lines[0][len('override KERNEL_CROSS_COMPILE := '):].rstrip('\r\n')
    suffix = '/' + COMPONENT
    if not old_kernel.startswith('/') or not old_kernel.endswith(suffix):
        raise ValueError('saved GPU kernel path is not an SDK component')
    old_sdk = old_kernel[:-len(suffix)]
    if not old_cross.startswith(old_sdk + '/out/toolchain/') or not old_cross.endswith('/bin/aarch64-linux-gnu-'):
        raise ValueError('saved GPU toolchain path is not the selected SDK toolchain')
    replacements = {
        kernel_lines[0]: kernel_lines[0].replace(old_kernel, str(sdk / COMPONENT), 1),
        cross_lines[0]: cross_lines[0].replace(old_cross, str(sdk) + old_cross[len(old_sdk):], 1),
    }
    relocated = ''.join(replacements.get(line, line) for line in lines).encode('utf-8')
    return {GPU_CONFIG: {'saved_sha256': inputs[GPU_CONFIG]['sha256'],
                         'relocated_sha256': hashlib.sha256(relocated).hexdigest(),
                         'old_sdk': old_sdk, 'new_sdk': str(sdk),
                         'fields': ['KERNELDIR', 'KERNEL_CROSS_COMPILE']}}


def git(root, *args):
    env = dict(os.environ, GIT_CONFIG_NOSYSTEM='1',
               GIT_CONFIG_GLOBAL='/dev/null', GIT_TERMINAL_PROMPT='0')
    return subprocess.check_output(
        ['git', '-c', 'core.hooksPath=/dev/null', '-c', 'core.fsmonitor=false',
         *args], cwd=root, env=env)


def unlinked(path):
    path = Path(path).absolute()
    for parent in (path, *path.parents):
        if parent.is_symlink():
            raise ValueError('linked reconstruction path')
    return path


def delivery_paths(sdk, stage):
    sdk, stage = unlinked(sdk), unlinked(stage)
    if sdk.parent.name != '.delivery' or stage.parent != sdk.parent or stage == sdk:
        raise ValueError('stage must be an independent child of the SDK delivery directory')
    if not (sdk / COMPONENT / '.git').exists():
        raise ValueError('selected kernel is not a Git component')
    return sdk, stage


def extract(archive, expected, destination, selected=None):
    seen = set()
    with tarfile.open(archive, 'r:gz') as stream:
        for member in stream:
            name = audit.relative(member.name)
            if name in seen or name not in expected or '.git' in Path(name).parts:
                raise ValueError('duplicate, unsafe or unlisted archive input')
            seen.add(name)
            desc = expected[name]
            if member.mode != desc['mode']:
                raise ValueError('archive input mode changed')
            if selected is not None and name not in selected:
                continue
            relative = selected[name] if selected is not None else name
            output = destination / audit.relative(relative)
            for parent in output.parents:
                if parent == destination:
                    break
                if parent.is_symlink():
                    raise ValueError('archive traverses a source symlink')
            output.parent.mkdir(parents=True, exist_ok=True)
            if output.exists() or output.is_symlink():
                raise ValueError('archive overwrites an existing source')
            if 'link' in desc:
                target = desc['link']
                if not member.issym() or member.linkname != target or target.startswith('/'):
                    raise ValueError('untrusted archive link')
                if destination not in (output.parent / target).resolve().parents:
                    raise ValueError('archive link escapes component')
                output.symlink_to(target)
            else:
                if not member.isfile() or member.size > 512 * 1024**2 or (
                        'size' in desc and member.size != desc['size']):
                    raise ValueError('archive input type or size changed')
                with stream.extractfile(member) as source, output.open('xb') as out:
                    shutil.copyfileobj(source, out)
                if audit.digest(output) != desc['sha256']:
                    raise ValueError('archive input bytes changed')
                output.chmod(desc['mode'])
    if seen != set(expected):
        raise ValueError('archive inputs missing')


def verify(root, record, full_modes=False):
    if git(root, 'rev-parse', 'HEAD').decode().strip() != record['commit']:
        raise ValueError('kernel commit changed')
    patch = git(root, 'diff', '--no-ext-diff', '--no-textconv', '--binary', '--full-index', 'HEAD')
    if hashlib.sha256(patch).hexdigest() != record['tracked_patch_sha256']:
        raise ValueError('kernel tracked source changed')
    others = set(git(root, 'ls-files', '--others', '--exclude-standard', '-z').decode().split('\0'))
    unknown = others - set(record['untracked']) - {'', RECEIPT}
    if unknown:
        raise ValueError('kernel has uncaptured untracked sources')
    for name, desc in record['untracked'].items():
        path = root / audit.relative(name)
        for parent in (path, *path.parents):
            if parent == root:
                break
            if parent.is_symlink() and parent != path:
                raise ValueError('source input has linked parent')
        if 'link' in desc:
            if not path.is_symlink() or os.readlink(path) != desc['link']:
                raise ValueError('saved source link changed')
        else:
            accepted = {desc['sha256']}
            relocation = record.get('generated_path_relocations', {}).get(name)
            if relocation and root == Path(record['sdk']) / COMPONENT:
                accepted.add(relocation['relocated_sha256'])
            if not path.is_file() or path.is_symlink() or audit.digest(path) not in accepted:
                raise ValueError('saved source input changed: ' + name)
        if path.lstat().st_mode & 0o777 != desc['mode']:
            raise ValueError('saved source input mode changed')
    if audit.digest(root / '.config') != record['config_sha256']:
        raise ValueError('saved effective kernel configuration changed')
    if (root / '.config').stat().st_mode & 0o777 != record['config_mode']:
        raise ValueError('saved effective kernel configuration mode changed')
    for name, desc in record.get('overlay_inputs', {}).items():
        path = root / audit.relative(name)
        unlinked(path.parent)
        if 'link' in desc:
            if not path.is_symlink() or os.readlink(path) != desc['link']:
                raise ValueError('overlay source link changed')
        elif not path.is_file() or path.is_symlink() or audit.digest(path) != desc['sha256']:
            raise ValueError('overlay source bytes changed')
        mask = 0o777 if full_modes else 0o111
        if path.lstat().st_mode & mask != desc['mode'] & mask:
            raise ValueError('overlay source mode changed')


def overlay_inputs(overlay, commit):
    if overlay is None:
        return {}, None
    overlay = unlinked(overlay)
    if not commit or git(overlay, 'rev-parse', 'HEAD').decode().strip() != commit:
        raise ValueError('overlay commit changed')
    captured = git(overlay, 'show', 'HEAD:meta/overlay-manifest.json')
    manifest_path = overlay / 'meta/overlay-manifest.json'
    if audit.digest(manifest_path) != hashlib.sha256(captured).hexdigest():
        raise ValueError('overlay manifest differs from pinned commit')
    manifest = audit.read_json(manifest_path)
    if manifest.get('schema_version') != 1:
        raise ValueError('unknown overlay manifest')
    entries = {}
    for name, desc in manifest['files'].items():
        audit.relative(name)
        if name.startswith(COMPONENT + '/'):
            relative = audit.relative(name[len(COMPONENT) + 1:])
            path = overlay / 'overlay' / name
            unlinked(path.parent)
            if 'link' in desc:
                if not path.is_symlink() or os.readlink(path) != desc['link']:
                    raise ValueError('saved overlay link changed')
                component_root = overlay / 'overlay' / COMPONENT
                if desc['link'].startswith('/') or component_root not in (path.parent / desc['link']).resolve().parents:
                    raise ValueError('saved overlay link escapes kernel component')
            elif not path.is_file() or path.is_symlink() or audit.digest(path) != desc['sha256']:
                raise ValueError('saved overlay bytes changed or LFS object unavailable')
            entries[relative] = {key: desc[key] for key in ('mode', 'size', 'sha256', 'link') if key in desc}
    if not entries:
        raise ValueError('overlay has no kernel inputs')
    return entries, hashlib.sha256(captured).hexdigest()


def cache_free(root):
    tracked = set(git(root, 'ls-files', '-z').decode().split('\0'))
    named = {'vmlinux', 'System.map', 'Module.symvers', '.version',
             'include/generated/autoconf.h', 'include/generated/utsrelease.h'}
    for path in root.rglob('*'):
        name = path.relative_to(root).as_posix()
        if '.git' in path.relative_to(root).parts:
            continue
        if path.is_file() and name not in tracked and (
                name in named or path.suffix in {'.o', '.cmd', '.ko', '.dtb'}):
            raise ValueError('compiler cache present in reconstructed component')
    if (root / '.git/objects/info/alternates').exists():
        raise ValueError('Git objects depend on another checkout')
    for path in (root / '.git/objects').rglob('*'):
        if path.is_file() and path.stat().st_nlink != 1:
            raise ValueError('Git objects are hardlinked')


def prepare(sdk, stage, snapshot, snapshot_pin, supplement, supplement_pin,
            overlay=None, overlay_commit=None):
    sdk, stage = delivery_paths(sdk, stage)
    snapshot, supplement = unlinked(snapshot), unlinked(supplement)
    if stage.exists():
        raise ValueError('reconstruction stage already exists')
    if audit.digest(snapshot / 'sdk-components.json') != snapshot_pin:
        raise ValueError('component snapshot pin changed')
    if audit.digest(supplement / 'supplement.json') != supplement_pin:
        raise ValueError('supplement pin changed')
    index = audit.read_json(snapshot / 'sdk-components.json')
    matches = [c for c in index['components'] if c['path'] == COMPONENT]
    if len(matches) != 1:
        raise ValueError('kernel component must be unique')
    component = matches[0]
    folder = snapshot / audit.relative(component['snapshot_directory'])
    inputs = audit.read_json(folder / 'inputs.json')
    if hashlib.sha256(audit.canonical(inputs)).hexdigest() != component['local_inputs_sha256']:
        raise ValueError('component input manifest changed')
    if (audit.digest(folder / 'tracked.patch') != inputs['tracked_patch_sha256'] or
            audit.digest(folder / 'untracked.tar.gz') != component['untracked_archive_sha256']):
        raise ValueError('saved component archive or patch changed')
    extra = audit.read_json(supplement / 'supplement.json')
    if audit.digest(supplement / 'inputs.tar.gz') != extra['archive_sha256']:
        raise ValueError('saved supplement archive changed')
    config = extra['files'][COMPONENT + '/.config']
    entries, manifest_pin = overlay_inputs(overlay, overlay_commit)
    tracked = set(git(sdk / COMPONENT, 'ls-files', '-z').decode().split('\0'))
    saved_untracked = dict(inputs['untracked'])
    for name, desc in entries.items():
        if name in saved_untracked:
            previous = saved_untracked[name]
            if any(previous.get(key) != desc.get(key) for key in ('mode', 'sha256', 'link')) or (
                    'size' in previous and 'size' in desc and previous['size'] != desc['size']):
                raise ValueError('snapshot and overlay input conflict')
        if name not in tracked:
            saved_untracked[name] = desc
    record = dict(requirement='REQ-MIMI-SDK-036', component=COMPONENT,
                  sdk=str(sdk), commit=component['commit'],
                  snapshot_sha256=snapshot_pin, supplement_sha256=supplement_pin,
                  tracked_patch_sha256=inputs['tracked_patch_sha256'],
                  untracked=saved_untracked, config_sha256=config['sha256'],
                  config_mode=config['mode'],
                  overlay_commit=overlay_commit, overlay_manifest_sha256=manifest_pin,
                  overlay_inputs=entries,
                  generated_path_relocations=relocated_gpu_config(
                      folder / 'untracked.tar.gz', inputs['untracked'], sdk),
                  phase='prepared', created_utc=time.time(), activation='not_run',
                  kernel_build='not_run', release_qualified=False)
    verify(sdk / COMPONENT, record)
    if shutil.disk_usage(sdk).free < 8 * 1024**3:
        raise ValueError('insufficient reconstruction reserve')
    git(sdk / COMPONENT, '-c', 'protocol.file.allow=always', 'clone', '--quiet',
        '--no-local', '--no-checkout', '--depth', '1', (sdk / COMPONENT).as_uri(), str(stage))
    git(stage, 'checkout', '--quiet', '--detach', component['commit'])
    git(stage, 'remote', 'remove', 'origin')
    if (folder / 'tracked.patch').stat().st_size:
        git(stage, 'apply', '--binary', '--whitespace=nowarn', str(folder / 'tracked.patch'))
    extract(folder / 'untracked.tar.gz', inputs['untracked'], stage)
    for name, desc in entries.items():
        source = Path(overlay) / 'overlay' / COMPONENT / name
        destination = stage / name
        unlinked(destination.parent)
        if destination.exists() or destination.is_symlink():
            if 'link' in desc:
                if not destination.is_symlink() or os.readlink(destination) != desc['link']:
                    raise ValueError('snapshot and overlay restored link conflict')
            elif not destination.is_file() or destination.is_symlink() or audit.digest(destination) != desc['sha256']:
                raise ValueError('snapshot and overlay restored bytes conflict')
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            if 'link' in desc:
                if desc['link'].startswith('/') or stage not in (destination.parent / desc['link']).resolve().parents:
                    raise ValueError('overlay link escapes reconstructed component')
                destination.symlink_to(desc['link'])
            else:
                shutil.copyfile(source, destination)
        if 'link' not in desc:
            destination.chmod(desc['mode'])
    extract(supplement / 'inputs.tar.gz', extra['files'], stage,
            {COMPONENT + '/.config': '.config'})
    verify(stage, record, full_modes=True)
    cache_free(stage)
    (stage / RECEIPT).write_text(json.dumps(record, indent=2) + '\n')
    return record


def activate(sdk, stage, retained, build_result):
    sdk, stage = delivery_paths(sdk, stage)
    retained = unlinked(retained)
    if sdk.parent not in retained.parents or sdk in retained.parents or stage in retained.parents:
        raise ValueError('retained tree must stay inside delivery and outside live sources')
    if retained.exists():
        raise ValueError('retained tree already exists')
    job = audit.read_json(unlinked(build_result))
    if job.get('phase') not in {'passed', 'failed', 'failed:source changes require review'} or 'finished_utc' not in job:
        raise ValueError('existing SDK build has not finished')
    for field in ['pid', 'child_pid']:
        pid = job.get(field)
        if not isinstance(pid, int) or pid <= 1 or Path('/proc', str(pid)).exists():
            raise ValueError('SDK build process is still live or unproven')
    ancestors = {os.getpid()}
    parent = os.getppid()
    while parent > 1 and parent not in ancestors:
        ancestors.add(parent)
        try:
            parent = int(Path('/proc', str(parent), 'stat').read_text().rsplit(')', 1)[1].split()[1])
        except (OSError, ValueError, IndexError):
            break
    for process in Path('/proc').iterdir():
        if not process.name.isdigit() or int(process.name) in ancestors:
            continue
        try:
            cwd = (process / 'cwd').resolve(strict=True)
            argv = (process / 'cmdline').read_bytes().split(b'\0')
        except OSError:
            continue
        program = Path(os.fsdecode(argv[0])).name if argv and argv[0] else ''
        compiler = program in {'make', 'cmake', 'ninja', 'cc1', 'cc1plus', 'ld', 'as'} or program.endswith(('gcc', 'g++'))
        if compiler and (cwd == sdk or sdk in cwd.parents or any(
                os.fsdecode(a).startswith(str(sdk) + '/') for a in argv)):
            raise ValueError('another SDK compiler process is active')
    record = audit.read_json(stage / RECEIPT)
    if record['sdk'] != str(sdk) or record['phase'] != 'prepared':
        raise ValueError('stage belongs to another SDK or is not prepared')
    verify(sdk / COMPONENT, record)
    verify(stage, record, full_modes=True)
    cache_free(stage)
    retained.parent.mkdir(parents=True, exist_ok=True)
    if retained.parent.stat().st_dev != stage.stat().st_dev:
        raise ValueError('activation requires atomic same-filesystem renames')
    old = sdk / COMPONENT
    old.rename(retained)
    try:
        stage.rename(old)
    except Exception:
        retained.rename(old)
        raise
    record.update(phase='activated', activation='passed', retained=str(retained),
                  activated_utc=time.time())
    (old / RECEIPT).write_text(json.dumps(record, indent=2) + '\n')
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['prepare', 'activate'])
    for option in ['sdk', 'stage', 'snapshot', 'supplement', 'retained', 'build-result', 'overlay']:
        parser.add_argument('--' + option, type=Path)
    parser.add_argument('--snapshot-sha256')
    parser.add_argument('--supplement-sha256')
    parser.add_argument('--overlay-commit')
    args = parser.parse_args()
    try:
        if args.sdk is None or args.stage is None:
            raise ValueError('selected SDK and stage required')
        if args.action == 'prepare':
            if None in (args.snapshot, args.supplement, args.snapshot_sha256, args.supplement_sha256,
                        args.overlay, args.overlay_commit):
                raise ValueError('saved input paths and exact pins required')
            record = prepare(args.sdk, args.stage, args.snapshot, args.snapshot_sha256,
                             args.supplement, args.supplement_sha256, args.overlay, args.overlay_commit)
        else:
            if args.retained is None or args.build_result is None:
                raise ValueError('retention path and terminal build receipt required')
            record = activate(args.sdk, args.stage, args.retained, args.build_result)
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, 'KERNEL REPLAY BLOCKED: ' + str(error) + '\n')
    print(json.dumps({k: v for k, v in record.items() if k not in {'untracked', 'overlay_inputs'}}, indent=2))


if __name__ == '__main__':
    main()
