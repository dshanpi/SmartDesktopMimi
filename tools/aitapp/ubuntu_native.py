"""Explicit Ubuntu extension ABI inspection; never executes candidate ELF/ldd.

Read the loader's program/dynamic tables, not attacker-replaceable section names.
This is compatibility preflight, not package trust or runtime authorization.
"""
import hashlib
import os
from pathlib import Path
import platform
import re
import stat
import struct
import subprocess

TARGET = {'os': 'ubuntu', 'architecture': 'x86_64', 'libc': 'glibc',
          'app_api': 1, 'profile': 'mimi-ubuntu-native-v1'}
LOADER = '/lib64/ld-linux-x86-64.so.2'
LIBRARIES = {'libc.so.6', 'libm.so.6', 'libpthread.so.0', 'libdl.so.2',
             'librt.so.1', 'libjson-c.so.4', 'libjson-c.so.5',
             'ld-linux-x86-64.so.2'}


def reject(message):
    raise ValueError('Ubuntu native: ' + message)


def target(value):
    if value != TARGET or not isinstance(value, dict) or type(value.get('app_api')) is not int:
        reject('explicit Ubuntu target/profile mismatch')


class Elf:
    def __init__(self, path, executable=True):
        self.path = Path(path)
        if not self.path.is_file() or self.path.stat().st_size > 8*1024*1024:
            reject('ELF file size/type')
        self.data = self.path.read_bytes()
        h = self.unpack('<16sHHIQQQIHHHHHH', 0)
        if h[0][:7] != b'\x7fELF\x02\x01\x01' or h[1] not in (2, 3) or h[2] != 62 or h[3] != 1 or h[8] != 64 or h[9] != 56 or not 1 <= h[10] <= 128:
            reject('ELF must be executable x86_64 little-endian ELF64')
        if executable and not h[4]:
            reject('missing executable entry')
        self.loads = []
        dynamic = []
        interp = []
        for i in range(h[10]):
            kind, flags, offset, addr, _, size, memsize, align = self.unpack('<IIQQQQQQ', h[5]+56*i)
            if size > memsize or offset+size > len(self.data):
                reject('program segment outside ELF')
            if kind == 1:
                if flags & 3 == 3:
                    reject('writable executable load segment')
                self.loads.append((addr, offset, size))
            elif kind == 2:
                dynamic.append((offset, size))
            elif kind == 3:
                interp.append(self.bytes(offset, size))
            elif kind == 0x6474e551 and flags & 1:
                reject('executable stack')
        if executable and interp != [LOADER.encode()+b'\0']:
            reject('unexpected or missing interpreter')
        if executable and not any(a <= h[4] < a+s for a, _, s in self.loads):
            reject('entry outside file-backed load segment')
        if len(dynamic) != 1 or dynamic[0][1] % 16:
            reject('dynamic table required')
        self.tags = {}
        ended = False
        for off in range(dynamic[0][0], sum(dynamic[0]), 16):
            tag, val = self.unpack('<QQ', off)
            if tag == 0:
                ended = True
                break
            if tag != 1 and tag in self.tags:
                reject('duplicate dynamic tag')
            self.tags.setdefault(tag, []).append(val)
        if not ended:
            reject('unterminated dynamic table')
        # RPATH/RUNPATH, TEXTREL, audit/filter/helper objects cannot override host.
        if any(t in self.tags for t in (15, 29, 22, 0x6ffffefc, 0x6ffffefb, 0x7fffffff, 0x7ffffffd)) or self.one(30, 0) & 4:
            reject('dynamic search path/text relocation/helper prohibited')
        self.strsize = self.one(10)
        if not 0 < self.strsize <= 1024*1024:
            reject('invalid string table')
        self.strbase = self.address(self.one(5), self.strsize)
        self.needed = [self.string(v) for v in self.tags.get(1, [])]
        if len(self.needed) > 16 or len(set(self.needed)) != len(self.needed) or not set(self.needed) <= LIBRARIES:
            reject('unknown or duplicate native dependency')
        self.soname = self.string(self.one(14)) if 14 in self.tags else None
        self.version_names = {}
        self.requirements = {}
        self.versions(0x6ffffffe, 0x6fffffff, needs=True)
        self.versions(0x6ffffffc, 0x6ffffffd, needs=False)
        self.imports, self.exports = self.symbols()

    def bytes(self, off, length):
        if off < 0 or length < 0 or off+length > len(self.data):
            reject('truncated ELF structure')
        return self.data[off:off+length]

    def unpack(self, fmt, off):
        return struct.unpack(fmt, self.bytes(off, struct.calcsize(fmt)))

    def one(self, tag, default=None):
        values = self.tags.get(tag)
        if values is None:
            if default is not None:
                return default
            reject('missing dynamic tag '+hex(tag))
        return values[0]

    def address(self, addr, size):
        matches = [o+addr-a for a, o, n in self.loads if a <= addr and addr+size <= a+n]
        if len(matches) != 1:
            reject('ambiguous/out-of-file dynamic address')
        return matches[0]

    def string(self, index):
        if index >= self.strsize:
            reject('string offset outside table')
        raw = self.data[self.strbase+index:self.strbase+self.strsize]
        end = raw.find(b'\0')
        if end < 0 or end > 512:
            reject('unterminated/oversized ELF name')
        try:
            value = raw[:end].decode('ascii')
        except UnicodeError:
            reject('non-ASCII ELF name')
        if any(ord(c) < 33 or ord(c) > 126 for c in value):
            reject('control character in ELF name')
        return value

    def versions(self, addr_tag, count_tag, needs):
        if (addr_tag in self.tags) != (count_tag in self.tags):
            reject('partial symbol version table')
        if addr_tag not in self.tags:
            return
        count = self.one(count_tag)
        if not 1 <= count <= 256:
            reject('version table limit')
        addr = self.one(addr_tag)
        for i in range(count):
            if needs:
                ver, cnt, name, aux, nxt = self.unpack('<HHIII', self.address(addr, 16))
                library = self.string(name)
                if library not in self.needed or library in self.requirements:
                    reject('version dependency not declared')
                self.requirements[library] = set()
            else:
                ver, flags, ndx, cnt, _, aux, nxt = self.unpack('<HHHHIII', self.address(addr, 20))
            if ver != 1 or not 1 <= cnt <= 256 or aux < (16 if needs else 20):
                reject('invalid version record')
            cursor = addr+aux
            for j in range(cnt):
                if needs:
                    _, flags, ndx, name, next_aux = self.unpack('<IHHII', self.address(cursor, 16))
                    value = self.string(name)
                    self.requirements[library].add(value)
                else:
                    name, next_aux = self.unpack('<II', self.address(cursor, 8))
                    value = self.string(name)
                if needs or j == 0:
                    index = ndx & 0x7fff
                    if index in self.version_names:
                        reject('duplicate version index')
                    self.version_names[index] = value
                if (j+1 < cnt) != bool(next_aux):
                    reject('broken version auxiliary chain')
                cursor += next_aux
            if (i+1 < count) != bool(nxt):
                reject('broken version chain')
            addr += nxt

    def symbol_count(self):
        if 4 in self.tags:
            addr = self.one(4)
            buckets, count = self.unpack('<II', self.address(addr, 8))
            if not 1 <= count <= 65536 or buckets > 65536:
                reject('symbol hash limit')
            self.address(addr, 8+4*(buckets+count))
            return count
        addr = self.one(0x6ffffef5)
        buckets, offset, bloom, _ = self.unpack('<IIII', self.address(addr, 16))
        if not 1 <= buckets <= 65536 or not 1 <= bloom <= 65536 or offset > 65536:
            reject('GNU hash limit')
        bucket_addr = addr+16+8*bloom
        values = self.unpack('<'+'I'*buckets, self.address(bucket_addr, 4*buckets))
        largest = max(values)
        if not largest:
            return offset
        if not offset <= largest < 65536:
            reject('GNU symbol index')
        chains = bucket_addr+4*buckets
        index = largest
        while index < 65536:
            value = self.unpack('<I', self.address(chains+4*(index-offset), 4))[0]
            index += 1
            if value & 1:
                return index
        reject('unterminated GNU hash chain')

    def symbols(self):
        count = self.symbol_count()
        # A forged hash table must not hide imports referenced by relocations.
        relocations = []
        for ptr, size, entry, expected in ((7, 8, 9, 24), (17, 18, 19, 16)):
            if ptr in self.tags:
                if self.one(entry) != expected:
                    reject('relocation size mismatch')
                relocations.append((self.one(ptr), self.one(size), expected))
        if 23 in self.tags:
            kind = self.one(20)
            if kind not in (7, 17):
                reject('unknown PLT relocation type')
            relocations.append((self.one(23), self.one(2), 24 if kind == 7 else 16))
        for addr, size, step in relocations:
            if size % step or size//step > 65536:
                reject('relocation table limit')
            off = self.address(addr, size)
            for n in range(0, size, step):
                _, info = self.unpack('<QQ', off+n)
                if info >> 32 >= count:
                    reject('relocation symbol outside hash bounds')
        if self.one(11) != 24:
            reject('unexpected symbol size')
        base = self.address(self.one(6), count*24)
        versions = None
        if 0x6ffffff0 in self.tags:
            versions = self.unpack('<'+'H'*count, self.address(self.one(0x6ffffff0), count*2))
        imports, exports = set(), set()
        for i in range(count):
            name, info, other, section, _, _ = self.unpack('<IBBHQQ', base+24*i)
            if not name or info >> 4 not in (1, 2, 10):
                continue
            text = self.string(name)
            idx = (versions[i] & 0x7fff) if versions else 1
            version = self.version_names.get(idx)
            if idx > 1 and version is None:
                reject('unknown symbol version index')
            if section == 0:
                if info >> 4 != 2:  # Undefined weak symbols may legitimately resolve to zero.
                    imports.add((text, version))
            elif other & 3 in (0, 3):
                exports.add((text, version))
                if not versions or not versions[i] & 0x8000:
                    exports.add((text, None))
        return imports, exports

    def receipt(self):
        return {'sha256': hashlib.sha256(self.data).hexdigest(),
                'needed': self.needed,
                'version_requirements': {k: sorted(v) for k, v in self.requirements.items()},
                'required_symbols': sorted([list(v) for v in self.imports], key=lambda v: (v[0], v[1] or ''))}


def controlled(path):
    original = Path(path)
    for p in (original,) + tuple(original.parents):
        s = p.lstat()
        if s.st_uid != 0 or (not stat.S_ISLNK(s.st_mode) and s.st_mode & 0o022):
            reject('untrusted host runtime path component')
    path = original.resolve(strict=True)
    for p in (path,) + tuple(path.parents):
        s = p.stat()
        if s.st_uid != 0 or s.st_mode & 0o022:
            reject('untrusted host runtime path')
    if not stat.S_ISREG(path.stat().st_mode):
        reject('host runtime must be regular file')
    return path


def inspect_host(elf):
    if platform.system() != 'Linux' or platform.machine() != 'x86_64':
        reject('host architecture mismatch')
    release = controlled('/etc/os-release').read_text()
    if not re.search(r'^ID=(?:ubuntu|"ubuntu")$', release, re.M):
        reject('host is not Ubuntu')
    preload = Path('/etc/ld.so.preload')
    if preload.exists() and controlled(preload).read_text().strip():
        reject('host preload is not supported by this profile')
    cache = controlled('/etc/ld.so.cache')
    command = controlled('/sbin/ldconfig')
    output = subprocess.run([str(command), '-p'], check=True, capture_output=True,
                            text=True, timeout=5, env={'PATH': '/usr/bin:/bin', 'LC_ALL': 'C'}).stdout
    resolved = {}
    for line in output.splitlines():
        m = re.fullmatch(r'\s*(\S+) \(([^)]*)\) => (\S+)\s*', line)
        if m and 'x86-64' in m[2] and m[1] not in resolved:
            resolved[m[1]] = m[3]
    loaded = {}
    pending = list(elf.needed) + ['ld-linux-x86-64.so.2']
    while pending:
        name = pending.pop()
        if name in loaded:
            continue
        if name not in LIBRARIES or name not in resolved:
            reject('missing/unsupported host dependency: '+name)
        path = controlled(resolved[name])
        if not str(path).startswith(('/usr/lib/x86_64-linux-gnu/', '/lib/x86_64-linux-gnu/')):
            reject('host library outside approved multiarch directories')
        lib = Elf(path, executable=False)
        if lib.soname != name:
            reject('host SONAME mismatch')
        loaded[name] = lib
        pending += lib.needed
    if controlled(LOADER) != loaded['ld-linux-x86-64.so.2'].path:
        reject('loader/cache mismatch')
    exports = set().union(*(x.exports for x in loaded.values()))
    for obj in [elf] + list(loaded.values()):
        for name, versions in obj.requirements.items():
            if name not in loaded or not versions <= set(loaded[name].version_names.values()):
                reject('missing host symbol version: '+name)
        if not obj.imports <= exports | elf.exports:
            reject('unresolved native symbols: '+str(sorted(obj.imports-(exports | elf.exports), key=str)[:4]))
    return {'target': TARGET, 'executable': elf.receipt(),
            'host_libraries': {k: dict(v.receipt(), path=str(v.path)) for k, v in loaded.items()},
            'ld_cache_sha256': hashlib.sha256(cache.read_bytes()).hexdigest(),
            'status': 'compatible_preflight_only'}
