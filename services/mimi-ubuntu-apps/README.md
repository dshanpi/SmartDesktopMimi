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
