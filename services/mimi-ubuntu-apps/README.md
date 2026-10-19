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
