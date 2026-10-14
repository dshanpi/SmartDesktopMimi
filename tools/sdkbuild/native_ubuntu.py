#!/usr/bin/env python3
"""Build the native Ubuntu desktop development target, not a release SDK/package.

The platform provider, enrolled updater and signed .deb pipeline must be qualified
separately. Never relabel this native development receipt as an A133 SDK receipt.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shlex
import shutil
import subprocess
import sys
from configure import resolutions
from source_ownership import audit_commands

ROOT = Path(__file__).resolve().parents[2]
PACKAGES = ('sdl2', 'SDL2_image', 'alsa', 'json-c', 'libcurl')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def preflight():
    fields = {}
    for line in Path('/etc/os-release').read_text().splitlines():
        key, sep, value = line.partition('=')
        if sep: fields[key] = shlex.split(value)[0]
    if fields.get('ID') != 'ubuntu' or platform.machine() != 'x86_64':
        raise ValueError('The native development target requires Ubuntu x86_64')
    missing = [name for name in ('cmake', 'gcc', 'g++', 'pkg-config', 'python3') if not shutil.which(name)]
    versions = {}
    if not missing:
        for name in PACKAGES:
            p = subprocess.run(['pkg-config', '--modversion', name], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if p.returncode: missing.append(name)
            else: versions[name] = p.stdout.strip()
    if missing: raise ValueError('Missing native build dependencies: ' + ', '.join(missing))
    return dict(platform_id='ubuntu-host', architecture='x86_64', target_os='ubuntu',
                target_os_version=fields['VERSION_ID'], pkg_config=versions)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--check', action='store_true')
    p.add_argument('--interactive', action='store_true')
    p.add_argument('--screen', action='append')
    p.add_argument('--voice-control', action='store_true')
    p.add_argument('--output', type=Path)
    a = p.parse_args()
    try:
        # Do not consume cross-compilation or host header/library overrides.
        for name in ('CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'LDFLAGS', 'CPATH', 'C_INCLUDE_PATH',
                     'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH', 'COMPILER_PATH', 'GCC_EXEC_PREFIX',
                     'PKG_CONFIG_PATH', 'PKG_CONFIG_LIBDIR', 'PKG_CONFIG_SYSROOT_DIR', 'CMAKE_TOOLCHAIN_FILE'):
            os.environ.pop(name, None)
        record = preflight()
        if a.check:
            print(json.dumps(record, indent=2)); return
        if a.interactive:
            a.screen = (input('Ubuntu host screen sizes, comma separated [1024x768]: ').strip() or '1024x768').split(',')
        screens = resolutions(a.screen or ['1024x768'])
        if a.output is None: raise ValueError('--output is required for a build')
        output = a.output.absolute()
        if output.exists() or output.is_symlink(): raise ValueError('Build output already exists')
        output.mkdir(parents=True)
        selected = ','.join(f"{s['width']}x{s['height']}" for s in screens)
        source = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
        dirty = bool(subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=normal'], cwd=ROOT, text=True).strip())
        if dirty: raise ValueError("Commit source before recording a native build")
        commands = [['cmake', '-S', str(ROOT/'apps/lv_port_linux'), '-B', str(output/'build'),
                     '-DCMAKE_C_COMPILER=/usr/bin/gcc', '-DCMAKE_CXX_COMPILER=/usr/bin/g++',
                     '-DPython3_EXECUTABLE=/usr/bin/python3', '-DCMAKE_BUILD_TYPE=Release',
                     '-DMIMI_APPLICATION_ONLY=ON', '-DAITVBOX_PLATFORM=ubuntu-host',
                     '-DMIMI_ENABLE_VOICE_CONTROL='+('ON' if a.voice_control else 'OFF'),
                     '-DMIMI_DISPLAY_PROFILES='+selected],
                    ['cmake', '--build', str(output/'build'), '-j4', '--target', 'lvglsim'],
                    ['cmake', '-S', str(ROOT/'services/mimi-ubuntu-provider'), '-B', str(output/'provider-build'),
                     '-DCMAKE_C_COMPILER=/usr/bin/gcc', '-DCMAKE_BUILD_TYPE=Release'],
                    ['cmake', '--build', str(output/'provider-build'), '-j4']]
        for i, command in enumerate(commands):
            with (output/f'build-{i}.log').open('w') as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        if source != subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() or subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=normal'], cwd=ROOT, text=True).strip():
            raise ValueError('Source changed during native build')
        commands_record = json.loads((output/'build/compile_commands.json').read_text())
        ownership = audit_commands(ROOT, commands_record)
        binary = output/'build/bin/lvglsim'
        linked = subprocess.check_output(['ldd', str(binary)], text=True)
        if 'not found' in linked: raise ValueError('Unresolved desktop runtime library')
        libraries = {}
        for line in linked.splitlines():
            parts = line.split()
            path = parts[2] if len(parts) > 2 and parts[1] == '=>' else parts[0]
            if path.startswith('/'): libraries[path] = sha(path)
        (output/'runtime-libraries.txt').write_text(linked)
        record.update(schema_version=1, kind='native-desktop-development-build', source_commit=source,
                      source_dirty=dirty, screens=screens, voice_control=a.voice_control,
                      desktop_sha256=sha(binary), compiler_sha256=sha('/usr/bin/gcc'),
                      provider_sha256=sha(output/'provider-build/mimi-ubuntu-provider'),
                      provider_kind='ubuntu-session-foundation', provider_in_application_payload=False,
                      runtime_libraries=libraries, source_ownership=ownership, provider_acceptance='not_run',
                      deb_install_acceptance='not_run', release_qualified=False)
        (output/'native-build.json').write_text(json.dumps(record, sort_keys=True, indent=2)+'\n')
        print(json.dumps(record, indent=2))
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        p.exit(1, 'NATIVE BUILD BLOCKED: '+str(error)+'\n')


if __name__ == '__main__': main()
