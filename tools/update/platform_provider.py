#!/usr/bin/env python3
"""Stage/build an independently audited A133 backend; not a migration certificate."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical(value):
    p = Path(value)
    if not p.is_absolute() or p.resolve() != p or any(c.isspace() for c in str(p)):
        raise ValueError('input path must be absolute, canonical and without whitespace')
    return p


def sdk_compiler(value, sdk):
    path = Path(value)
    if not path.is_absolute() or str(path) != str(Path(os.path.normpath(path))) or path.name != 'aarch64-openwrt-linux-gcc' or within(path, sdk) is None:
        raise ValueError('invalid fixed A133 compiler path')
    target = path.resolve(strict=True)
    if within(target, sdk) is None:
        raise ValueError('compiler alias escapes SDK')
    regular(target)
    # OpenWrt wrappers dispatch using argv0. Keep the reviewed logical name,
    # while pinning both its resolved path and bytes instead of changing argv0.
    return path


def regular(path):
    if not path.is_file() or path.is_symlink():
        raise ValueError('source/input must be a regular non-symlink file: ' + str(path))
    return path


def within(path, root):
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return None


def dependencies(raw):
    text = raw.decode('utf-8').replace('\\\n', ' ')
    target, sep, tail = text.partition(':')
    if not sep or target.strip() != 'mimi_deps':
        raise ValueError('unexpected preprocessor dependency format')
    return [Path(s) for s in shlex.split(tail)]


def check_argv(args):
    if not args or any(any(c in x for c in ('\x00', '\n', ';', '`')) or x.startswith('@') or 'plugin' in x or x == '-wrapper' or x == '&&' for x in args):
        raise ValueError('unsupported command/response/plugin input')


def remove_output(args):
    out = []
    i = 1
    while i < len(args):
        if args[i] == '-o':
            if i + 1 >= len(args):
                raise ValueError('missing object output')
            i += 2
            continue
        if args[i] != '-c':
            out.append(args[i])
        i += 1
    return out


def run(args, cwd, log=None):
    check_argv(list(map(str, args)))
    env = dict(os.environ)
    # Compiler lookup and staging are explicit; host include overrides cannot
    # alter the recorded source/runtime boundary.
    for name in ('CPATH', 'C_INCLUDE_PATH', 'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH', 'COMPILER_PATH', 'GCC_EXEC_PREFIX', 'LD_PRELOAD'):
        env.pop(name, None)
    env['PATH'] = str(Path(args[0]).parent) + ':/usr/bin:/bin'
    for arg in args:
        if str(arg).startswith('--sysroot='):
            env['STAGING_DIR'] = str(Path(str(arg).split('=', 1)[1]).parent)
    p = subprocess.run(list(map(str, args)), cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=90)
    if log is not None:
        log.write_bytes(p.stdout + p.stderr)
    if p.returncode:
        raise ValueError('compiler/linker failed, returncode=' + str(p.returncode))
    return p.stdout


def normalize(arg, roots):
    for name, root in sorted(roots.items(), key=lambda row: -len(str(row[1]))):
        arg = arg.replace(str(root), '${' + name + '}')
    return arg


def expand(arg, roots):
    for name, root in roots.items():
        arg = arg.replace('${' + name + '}', str(root))
    if '${' in arg:
        raise ValueError('unknown recipe placeholder')
    return arg


def audit_dependencies(paths, source, sdk, private, origin=None):
    files = {}
    for path in paths:
        if not path.is_absolute() or path.resolve() != Path(os.path.normpath(path)):
            # SDK header symlinks are real selected SDK inputs; resolve them,
            # while project/private source aliases never become accepted.
            if path.is_absolute() and within(path, sdk) is not None:
                path = path.resolve()
            else:
                raise ValueError('noncanonical dependency: ' + str(path))
        path = Path(os.path.normpath(path))
        if origin is not None and within(path, origin) is not None:
            raise ValueError('standalone component read original application source')
        bucket = None
        for name, root in [('project', source), ('sdk', sdk), ('private', private)]:
            rel = within(path, root)
            if rel is not None:
                bucket = name + '/' + rel
                break
        if bucket is None:
            raise ValueError('unclassified compiler dependency: ' + str(path))
        regular(path)
        files[bucket] = sha(path)
    return files


def profile_sources(rows, profile, source, private):
    expected_count = {'private': 32, 'public': 30}[profile]
    if len(rows) != expected_count:
        raise ValueError('backend translation unit count does not match selected cloud profile')
    names = {within(Path(row['file']), source) for row in rows}
    external = [Path(row['file']) for row in rows if within(Path(row['file']), source) is None]
    cloud = 'apps/lv_port_linux/src/system/service_cloud'
    if profile == 'private':
        if cloud + '.c' not in names or cloud + '_validation.c' not in names or cloud + '_stub.c' in names or external != [private / 'src/100ask_iot.c']:
            raise ValueError('private cloud source composition mismatch')
    elif cloud + '_stub.c' not in names or cloud + '.c' in names or cloud + '_validation.c' in names or external:
        raise ValueError('public profile includes private cloud sources')


def stage(args):
    source, sdk, private = [canonical(getattr(args, k)) for k in ('source', 'sdk', 'private_sdk')]
    compiler = sdk_compiler(args.compiler, sdk)
    output = Path(args.output).absolute()
    if output.exists() or output.is_symlink():
        raise ValueError('output already exists')
    if within(output, source) is not None:
        raise ValueError('component output must be outside source checkout')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip()
    if commit != args.source_commit or subprocess.check_output(['git', 'status', '--porcelain'], cwd=source, text=True).strip():
        raise ValueError('source commit/clean state mismatch')
    if within(compiler, sdk) is None or within(private, source) is not None:
        raise ValueError('compiler must belong to SDK; private SDK must be outside source')
    sys.path.insert(0, str(source / 'tools/update'))
    from source_ownership import verify, owner
    policy = verify(source)
    build = canonical(args.build_dir) if args.build_dir else source / 'apps/lv_port_linux/build'
    commands_file = build / 'compile_commands.json'
    link_file = build / 'CMakeFiles/lv_backend.dir/link.txt'
    commands = json.loads(regular(commands_file).read_text())
    rows = [c for c in commands if 'CMakeFiles/lv_backend.dir/' in c.get('command', '')]
    profile_sources(rows, args.profile, source, private)
    cache = (build / 'CMakeCache.txt').read_text()
    expected_flag = 'ON' if args.profile == 'private' else 'OFF'
    if 'AITVBOX_ENABLE_100ASK_CLOUD:BOOL=' + expected_flag + '\n' not in cache:
        raise ValueError('CMake cloud flag differs from explicit component profile')
    output.mkdir(mode=0o700, parents=True)
    (output / 'code').mkdir(mode=0o700)
    roots = dict(SOURCE_ROOT=source, SDK_ROOT=sdk, PRIVATE_SDK=private)
    entries = []
    inputs = {}
    objects = {}
    source_files = {}
    for i, row in enumerate(rows):
        cfile = canonical(row['file'])
        project_rel = within(cfile, source)
        if project_rel is not None:
            kind = owner(policy, project_rel)
            if kind == 'platform-client-frozen' and not policy['files'][project_rel].get('sha256'):
                raise ValueError('unfixed shared client')
        elif within(cfile, private) is None:
            raise ValueError('external translation unit outside fixed private dependency')
        regular(cfile)
        argv = shlex.split(row['command'])
        check_argv(argv)
        # Ignore the generated compiler wrapper; only the explicit SDK compiler
        # is executed, and its actual hash becomes a required recipe input.
        flags = remove_output(argv)
        paths = dependencies(run([str(compiler), *flags, '-M', '-MT', 'mimi_deps'], Path(row['directory'])))
        inputs.update(audit_dependencies(paths, source, sdk, private))
        objects[argv[argv.index('-o') + 1]] = '${BUILD_ROOT}/object-' + str(i) + '.o'
        entries.append(dict(source=normalize(str(cfile), roots), flags=[normalize(v, roots) for v in flags]))
    if args.profile == 'public' and any(key.startswith('private/') for key in inputs):
        raise ValueError('public component reads private dependency')
    for key, digest in inputs.items():
        if key.startswith('project/'):
            rel = key[len('project/'):]
            path = source / rel
            target = output / 'code' / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(path.read_bytes())
            target.chmod(0o600)
            if sha(target) != digest:
                raise ValueError('source changed during staging')
            source_files[rel] = digest
    link = shlex.split(regular(link_file).read_text())
    check_argv(link)
    link_flags = []
    i = 1
    while i < len(link):
        value = link[i]
        if value == '-o':
            link_flags += ['-o', '${BUILD_ROOT}/lv_backend']
            i += 2
            continue
        if value.endswith('.o'):
            if value not in objects:
                raise ValueError('unclassified link object')
            value = objects[value]
        link_flags.append(normalize(value, roots))
        i += 1
    recipe = dict(schema_version=1, cloud_profile=args.profile, requirement='REQ-MIMI-SPLIT-053', source_commit=commit, tool_sha256=sha(Path(__file__).resolve()),
                  source_ownership_sha256=sha(source / 'docs/update/source-ownership.json'),
                  origin_root=str(source), compiler_sha256=sha(compiler), compiler_target=within(compiler.resolve(), sdk),
                  compile_commands_sha256=sha(commands_file), link_recipe_sha256=sha(link_file),
                  original_backend_sha256=sha(build / 'bin/lv_backend'), sources=source_files,
                  inputs=inputs, translation_units=entries, link_flags=link_flags,
                  migration_complete=False, sdk_overlay_integrated=False, release_qualified=False)
    (output / 'provider-recipe.json').write_text(json.dumps(recipe, sort_keys=True, indent=2) + '\n')
    # Only source/recipe is staged; no original executable, object, SDK header,
    # vendor code or actual credentials enters the standalone source tree.
    print(json.dumps(dict(status='passed', scope='source closure staged only', sources=len(source_files), translation_units=len(entries))))


def build_component(args):
    component, sdk, private = [canonical(getattr(args, k)) for k in ('component', 'sdk', 'private_sdk')]
    compiler = sdk_compiler(args.compiler, sdk)
    recipe = json.loads(regular(component / 'provider-recipe.json').read_text())
    if recipe['schema_version'] != 1 or recipe['tool_sha256'] != sha(Path(__file__).resolve()) or sha(compiler) != recipe['compiler_sha256'] or within(compiler.resolve(), sdk) != recipe['compiler_target']:
        raise ValueError('compiler or schema mismatch')
    if within(compiler, sdk) is None or within(private, component) is not None:
        raise ValueError('wrong SDK compiler/private boundary')
    origin = canonical(recipe['origin_root'])
    for name, digest in recipe['sources'].items():
        p = component / 'code' / name
        if within(p, component / 'code') is None or sha(regular(p)) != digest:
            raise ValueError('staged source differs from receipt')
    expected_external = {k: v for k, v in recipe['inputs'].items() if not k.startswith('project/')}
    for name, digest in expected_external.items():
        bucket, rel = name.split('/', 1)
        p = {'sdk': sdk, 'private': private}[bucket] / rel
        if p.resolve() != p or sha(regular(p)) != digest:
            raise ValueError('fixed SDK/private build input changed')
    output = Path(args.output).absolute()
    if output.exists() or output.is_symlink() or within(output, component) is not None:
        raise ValueError('build output must be a new independent directory')
    output.mkdir(mode=0o700, parents=True)
    roots = dict(SOURCE_ROOT=component / 'code', SDK_ROOT=sdk, PRIVATE_SDK=private, BUILD_ROOT=output)
    observed = {}
    for i, entry in enumerate(recipe['translation_units']):
        flags = [expand(v, roots) for v in entry['flags']]
        # Preserve existing __FILE__ diagnostics without reading any old source;
        # actual dependency auditing below still rejects original source reads.
        flags.append('-fmacro-prefix-map=' + str(component / 'code') + '=' + str(origin))
        paths = dependencies(run([str(compiler), *flags, '-M', '-MT', 'mimi_deps'], output))
        observed.update(audit_dependencies(paths, component / 'code', sdk, private, origin))
        run([str(compiler), *flags, '-c', '-o', str(output / ('object-' + str(i) + '.o'))], output, output / ('compile-' + str(i) + '.log'))
    if recipe['cloud_profile'] == 'public' and any(key.startswith('private/') for key in observed):
        raise ValueError('public profile read private dependencies')
    if observed != recipe['inputs']:
        raise ValueError('standalone source/include closure differs from original')
    run([str(compiler), *[expand(v, roots) for v in recipe['link_flags']]], output, output / 'link.log')
    for name, digest in recipe['sources'].items():
        if sha(component / 'code' / name) != digest:
            raise ValueError('component changed during build')
    for name, digest in expected_external.items():
        bucket, rel = name.split('/', 1)
        path = {'sdk': sdk, 'private': private}[bucket] / rel
        if sha(regular(path)) != digest:
            raise ValueError('SDK/private input changed during build')
    actual = sha(output / 'lv_backend')
    result = dict(requirement='REQ-MIMI-SPLIT-053', status='passed',
                  scope='Actual independent A133 compile/link with audited deps; not device compatibility or completed repository split', cloud_profile=recipe['cloud_profile'],
                  source_commit=recipe['source_commit'], recipe_sha256=sha(component / 'provider-recipe.json'),
                  compiler_sha256=recipe['compiler_sha256'], backend_sha256=actual,
                  original_backend_sha256=recipe['original_backend_sha256'], byte_identical=actual == recipe['original_backend_sha256'],
                  sources=len(recipe['sources']), translation_units=len(recipe['translation_units']),
                  no_original_source_dependency=True, private_vendor_copied=False,
                  sdk_overlay_integrated=False, migration_complete=False, release_qualified=False)
    (output / 'build-result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    commands = p.add_subparsers(dest='command', required=True)
    s = commands.add_parser('stage')
    s.add_argument('--source', required=True)
    s.add_argument('--source-commit', required=True)
    s.add_argument('--profile', choices=['private', 'public'], default='private')
    s.add_argument('--build-dir')
    b = commands.add_parser('build')
    b.add_argument('--component', required=True)
    for child in (s, b):
        child.add_argument('--sdk', required=True)
        child.add_argument('--private-sdk', required=True)
        child.add_argument('--compiler', required=True)
        child.add_argument('--output', required=True)
    args = p.parse_args()
    try:
        stage(args) if args.command == 'stage' else build_component(args)
    except (ValueError, KeyError, OSError, subprocess.SubprocessError) as e:
        p.exit(1, 'PLATFORM SOURCE BLOCKED: ' + str(e) + '\n')


if __name__ == '__main__':
    main()
