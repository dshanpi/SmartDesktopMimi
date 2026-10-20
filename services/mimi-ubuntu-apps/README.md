# Ubuntu extension platform installer (UBUNTUAPPS076)

This is independently provisioned platform code. It is **not** part of a
shared desktop update, and is not yet wired to the production login profile.
The first admission profile is a signed schema-1 embedded page with no platform
permissions. Schema-2 AArch64 native programs and unavailable capabilities fail
closed. This prerequisite does not complete native target admission, sandboxed
execution, storage broker, media or the full application data acceptance matrix.

Provision the installer and `tools/aitapp/aitapp.py` in a root-owned tree retaining
their repository-relative locations. All ancestors must be root-owned and not
writable by other users. Running the privileged CLI from a writable developer
checkout is refused. Provision a separate root-owned apps directory and trusted
publisher public keys; no key generation or trust enrollment is performed by the
installer. The root-controlled configuration has exactly these fields:

```json
{
  "schema": 1,
  "apps_root": "/var/lib/smartdesktopmimi/apps",
  "trusted_keys": ["/etc/smartdesktopmimi/app-publishers/approved.pem"]
}
```

These are example paths, not an installed configuration. Keys and configuration
must remain outside the apps root. Invoke the independently installed script:

```text
python3 services/mimi-ubuntu-apps/installer.py --config PLATFORM_CONFIG install PACKAGE.aitapp
python3 services/mimi-ubuntu-apps/installer.py --config PLATFORM_CONFIG recover
```

The installer snapshots caller-controlled bytes into private bounded storage,
checks archive shape/size and complete signed checksums using the shared package
parser, and pins application ownership to the signer's DER fingerprint. Upgrades
must increase the numeric version and keep the same signer. Nothing executes from
the package. Data lives at `<apps_root>/<id>/data`, outside desktop releases.
Symlinks, hardlinks, foreign/writable ownership and excessive data are refused.
Code is readable by the desktop and installed without execute/write permission;
data starts root-only for the future broker. The browser/frontend has no privileged
install bypass.

A single no-follow flock serializes installers and recovery. The future storage
broker **must take the same lock** while resolving/writing application data;
otherwise this install implementation must not be connected to that broker.
Copying existing data is bounded to 64 MiB and 4096 files. A durable journal records
only application identity, internal directory token and previous metadata digest.
Before journal removal, process interruption rolls back to the old installation
(or no installation for the first install). After durable commit, the new version
wins. Recovery is itself restartable. Interrupted preparation/committed cleanup may
leave hidden staging/history directories; they are not scanned as apps and are
retained for diagnosis, not automatically trusted or activated. Disk reclamation
and user-requested extension rollback/removal remain future work.

Run `sudo python3 tests/sdkbuild/test_ubuntu_apps_installer.py -v` for isolated
root filesystem tests. Tests generate ephemeral publisher keys under a temporary
`/root` directory, remove them afterwards, and never enroll them into a product.
They cover real RSA verification, data copying, publisher/version rejection,
controlled CLI deployment and eight forced process exits at transaction phases.
They are not physical power-loss, native execution or real-user data acceptance.

## Experimental native execution primitive

`native_runner.c` is an independent Ubuntu Linux x86_64 platform prerequisite,
not an activated service or package admission path. The installer still refuses
native packages. Build with `cc -std=gnu11 -O2 -Wall -Wextra -Werror
services/mimi-ubuntu-apps/native_runner.c -o mimi-native-runner` and run the actual
process tests with `sudo python3 tests/sdkbuild/test_ubuntu_native_runner.py -v`.
No A133 SDK implementation was copied into this component.

The future root-controlled broker must verify the signed installation, publisher,
explicit native ABI/dependencies and permissions under the installer lock before
opening immutable root-owned code read-only. It invokes the root-owned runner as
`mimi-native-runner --executable-fd FD --action ACTION --socket ABSOLUTE_PATH`,
with stdout connected to a pipe. The broker must cap/read the response (4 KiB),
validate its JSON and authenticate every capability request against the actual
native child PID, parent monitor and admitted application. The ELF header check
alone is not ABI, signature or dependency validation. Do not install setuid or
expose a user-writable root deployment. Neither production integration nor these
future broker responsibilities are provided by this primitive.

A small root monitor forks a trusted setup stub. The stub clears environment,
extra descriptors and groups, drops to UID/GID 65534, sets resource limits and
no-new-privileges, then installs a deny-default syscall filter with a seccomp
notification listener. Only the stub's first execveat notification is continued;
all later execution requests are denied, including re-execution of the same FD
and absolute-path overrides. This use of CONTINUE does not inspect mutable native
application pointers: at first exec no untrusted code or threads have run. The
listener crosses a private socketpair; its reserved FD cannot be closed/replaced
or reconnected and its monitor-side peer is closed before native execution.
Unsupported notification kernels fail closed. Requires Linux x86_64 seccomp user
notification plus CONTINUE support (tested here on Ubuntu kernel 5.15).

Native code has a 2-second CPU, 128 MiB address-space and 32-descriptor limit, no
core/file output and no process creation. A separate 5-second wall timer bounds
startup, execution and notification handling. Monitor exit follows caller exit;
native exit follows monitor exit. Exit 124 means wall timeout, 125 invalid entry,
126 setup/monitor failure, 127 initial exec failure, and 128+signal a signal-killed
native process. Ordinary native exit codes are relayed and are not trusted success
claims. The root monitor does not parse application requests or write app data.

This is not filesystem/IPC namespace isolation: world-readable files and allowed
Unix stream services are accessible under normal DAC permissions, and UID 65534
is shared. Direct Internet sockets, writable filesystem opens, device ioctls,
process creation, filter changes and subsequent exec are refused. It is suitable
only as a prerequisite under the approved-publisher trust model, not arbitrary
hostile publisher isolation. Per-application storage authorization, bounded
response consumption and complete signed native admission remain mandatory
before activation. Tests use isolated /var/lib fixtures because /run is noexec;
no mount restrictions or product trust are changed.

Schema-3 Ubuntu native build/sign/host compatibility preflight is now available
in `tools/aitapp/README.md`. It preserves schema-2 AArch64 meaning and checks real
host symbol/library dependencies. This installer still refuses native packages:
authenticated execution/storage broker and trusted session integration remain
required before activation. A successful preflight does not change that policy.


## Internal storage backend (not an exposed capability)

`storage.py` is a platform-internal prerequisite with no CLI or socket listener.
It does not authenticate callers or grant permissions. The future broker must
verify a signed installed application, actual native process identity and
`storage.app` permission before passing its trusted `(app_id, publisher_sha256,
installed_digest)` tuple. Never use an identity supplied in an application RPC.
Native installation and production session activation remain closed.

`Storage(installer).operate(identity, arguments)` takes the same transaction lock
as installation, recovers pending installs, and pins the previously admitted
installation. Upgrading invalidates an old tuple; a new admitted tuple can read
the preserved data. Operations are `read` (key) and `write` (key, value), with the
public SDK result shape. Keys are JSON keys, never paths. There are at most 128
keys, 64 KiB serialized state and 3500 bytes per JSON-encoded text value, within
the SDK transport envelope. UTF-8 is preserved; embedded NUL, duplicate fields,
unknown schemas, corrupt state and excessive values are rejected without
replacing the existing state. The complete app data quota also applies while
holding temporary replacement bytes.

State is a root-only `data/storage-v1.json` beside other application data. Writes
use exclusive private temporary files, fsync, atomic replacement and directory
fsync. Only validated temporary names owned by this backend are cleaned. Process
interruption produces complete old or new state; this does not establish physical
power-loss durability on arbitrary filesystems. Other data is retained.

Run `sudo python3 tests/sdkbuild/test_ubuntu_storage.py -v`. The tests use actual
signed schema-1 installs as internal storage fixtures, without enabling a storage
permission for those apps. They check restart/UTF-8, corruption and unsafe path
rejection, quotas, independent apps, signed upgrades, installer recovery, real
process interruptions and concurrent installer lock exclusion. Native SDK RPC,
authentication, permission integration and real-user data acceptance remain open.


## Internal authenticated native invocation (not production activation)

`broker.py` connects signed native admission, restricted execution and storage.
`NativeBroker(installer, runner, runtime_root).invoke(id, action)` is an internal
trusted-platform API. `DesktopEndpoint` adds a root-owned Unix socket handler for
the existing desktop `invoke` JSON request shape. It accepts only the pinned,
root-owned desktop executable, checks kernel peer credentials and confirms that
the peer belongs to an active local graphical logind seat before and after
reading a bounded request. It permits only a signed UI action and lets the broker
check the package publisher, target, permission and payload again.

This endpoint is source-level integration only. There is no production service
unit, root configuration, socket path activation, package profile wiring or
desktop binary pin provisioned by this change. The native installer still
rejects schema-3 packages. Before an administrator enables the endpoint, install
the broker, installer, storage and package parser as one complete root-owned
tree, pin the actual installed desktop executable and configure the desktop
socket path to the endpoint. Desktop upgrades must refresh the pin and restart
the service; a mismatch fails closed. Tests did not force or modify an active
login.

The broker locks and recovers installation, snapshots code excluding runtime data
and the publisher marker, checks the complete signed payload, schema-3 target,
actual host libraries/symbols, publisher pin and signed UI action. Only an empty
permission set or `storage.app` is presently available. An immutable root-owned
read-only executable FD is passed to the restricted runner; no package code is
run during admission. Runtime directories must be on an executable filesystem
(the development host `/run` is noexec); no mount restrictions are changed.
Each invocation uses a fresh root-controlled directory and Unix capability socket,
then removes both on exit. Socket naming is not an authentication mechanism.

For every capability connection the broker checks kernel SO_PEERCRED, UID/GID
65534, the known live runner parent, process start times and actual executable
inode, empty supplementary groups/capabilities, no-new-privileges and seccomp.
The process is checked again after bounded frame reception. Another process with
the same UID, or another runner using the same executable inode, is refused.
Requests cannot supply an application ID: the broker supplies the signed/pinned
identity and checks the manifest permission before calling internal storage.
A changed installation invalidates an in-flight storage identity; it is never
silently rebound to the new version.

Frames and native stdout are limited to 4 KiB, each request frame to a total
200 ms, each action to the runner's 5-second deadline (6-second broker backstop),
and each invocation to 64 connections / 32 capability requests. Duplicate or
unknown fields, extra JSON documents, unsupported capabilities and oversized
responses are rejected. Only the public SDK `{ok:boolean,message:string}` reply
shape is accepted from native stdout. Error responses do not include paths or
application payloads. Existing runner filesystem/Unix-socket isolation limits
still apply; this is not hostile-publisher container isolation.

`sudo python3 tests/sdkbuild/test_ubuntu_broker.py -v` compiles and signs a real
public-SDK application under temporary root-controlled fixtures, exercises actual
SDK write/read across invocations and kernel peer authentication, and tests
revocation after a signed fixture version change, denied permissions, malformed
frames, floods, deadlines and cleanup. These fixtures deliberately do not bypass
production installer policy. The session endpoint tests use the real kernel
credentials and process executable identity with a mocked active-seat result;
they do not prove the installed Mimi binary/profile is connected to a live GDM
session. Complete native installation/profile deployment and full real-user
upgrade/rollback data acceptance remain required before product activation.
