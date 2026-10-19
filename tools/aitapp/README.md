# Signed extension build tool

Legacy schema 1 embedded pages and schema 2 AArch64 native applications retain
their meaning. `init --native` and `compile --sdk TINA_SDK` remain A133 tools.
Schema 3 introduces an explicit Ubuntu x86_64 native target. Nothing infers a
platform from the developer workstation, changes an ELF machine number, or
silently reinterprets a legacy package.

## Ubuntu native prerequisite workflow

On an Ubuntu x86_64 build host with gcc, libjson-c-dev and openssl:

```sh
python3 tools/aitapp/aitapp.py init example --id com.example.notes --name Notes --publisher approved-publisher --native-ubuntu
python3 tools/aitapp/aitapp.py compile-ubuntu example --receipt /path/out/build.json
python3 tools/aitapp/aitapp.py build example --key /approved/publisher-private.pem -o /path/out/notes.aitapp
python3 tools/aitapp/aitapp.py verify-ubuntu /path/out/notes.aitapp --public-key /approved/publisher-public.pem --receipt /path/out/preflight.json
```

Paths are placeholders. Use existing approved publisher keys outside the source
and output tree; this workflow does not create or enroll product keys. The host
build receipt must be kept outside the application payload. `verify` checks
signature/payload and structural target validity; `verify-ubuntu` additionally
checks actual current Ubuntu runtime compatibility and returns **preflight only**.
The platform installer still refuses native packages until the authenticated
broker/storage integration is complete. No native installation/activation option
or production service is introduced here.

Schema 3 requires this exact `target`, along with the existing native `runtime`
(type native-rpc, bin entry and integer API 1), permissions and embedded UI:

```json
{"os":"ubuntu","architecture":"x86_64","libc":"glibc","app_api":1,"profile":"mimi-ubuntu-native-v1"}
```

The first profile uses a dynamic 64-bit little-endian x86_64 executable and
`/lib64/ld-linux-x86-64.so.2`. Structural inspection reads program/dynamic tables,
including dependencies, version needs, hash-bounded dynamic symbols and relocation
indices; changing section headers cannot conceal a dependency. It never runs the
candidate or ldd. Writable-executable segments, executable stack, RPATH/RUNPATH,
text relocations and loader audit/filter/helper objects are refused. Only distro
glibc/libm/pthread/dl/rt and json-c SONAMEs 4 or 5 are admitted, and the exact SONAME
must exist on the target. No bundled library/loader or extra executable is allowed.

Host preflight checks Ubuntu ID and x86_64, resolves the actual loader cache,
checks root-controlled host paths and approved multiarch locations, recursively
loads dependencies and matches symbol versions and required strong symbols.
Undefined weak symbols may resolve to zero under normal ELF rules. Nonempty
system ld.so.preload is refused. The receipt fixes actual executable, library and
loader-cache hashes. A json-c 5 build is not claimed compatible with a json-c 4
host, nor does a new glibc build acquire backward compatibility by changing text.

Preflight describes those exact package and host bytes at that time. A future
privileged broker must snapshot package input, reverify signature/publisher,
recheck ABI/dependency identity under its installation/execution lock, validate
permissions and authorize each request before launch. These checks are not a
security proof for arbitrary publishers, a substitute for the restricted runner,
or evidence of production app data retention. Static ELF, other libc/platforms,
C++ runtimes and additional dependencies require a new explicit reviewed profile.
