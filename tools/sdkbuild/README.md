# Developer build selection

This is a developer-host tool. There is no corresponding desktop screen.

Run `python3 tools/sdkbuild/configure.py configure --interactive --output PROFILE.json`
and provide the SDK path, independently reviewed SDK lock hash, platform,
target OS and comma-separated screen list. Configuration checks every SDK
input and compiles/links a dependency probe against the selected target SDK.
It never runs a target executable on the host. Reviewed profiles cannot be
silently overwritten.

Use `check --profile PROFILE.json --sdk SDK_DIRECTORY` to repeat validation;
use `build --profile PROFILE.json --sdk SDK_DIRECTORY --output NEW_BUILD_DIRECTORY`
for a clean-source application build. The output includes a build profile
and the desktop receipt records the compiled screen list. A device outside that
list cannot report healthy startup.

The A133 Tina aarch64 compiler adapter has actual build evidence. T527/RK3576
remain unavailable. A Debian/Ubuntu compiler adapter and deb transport builder
are implemented, but their real target SDK, Provider integration, device install,
upgrade/rollback and physical qualification remain **not_run**. The voice_control flag includes
the optional navigation app and is recorded in its receipt. Runtime voice is
off by default; cloud ASR requires the existing bound Tuya service. Selecting a
resolution is not layout or hardware acceptance: only 1024x768 has current
A133 evidence; host renders of additional sizes do not qualify physical panels.

## Explicit Debian/Ubuntu SDK inputs

Legacy SDK schema 1 is Tina only. Schema 2 additionally pins `target_os`,
`target_os_version` and `compiler_prefix=aarch64-linux-gnu-`. Its inventoried
`sysroot/usr/lib/os-release` must match the requested OS/version. Missing
compiler tools, mismatched compiler target, altered inputs, wrong baseline,
missing headers or dependency link failures block configuration/build.
Multiarch include/library/pkg-config paths stay inside the selected sysroot.
No target executable is run on the development host.

Export a reviewed, relocatable target SDK with:

```sh
python3 tools/update/application_sdk.py export --sdk NEW_SDK_DIR \
  --toolchain TOOLCHAIN_DIR --sysroot TARGET_SYSROOT --baseline REVIEWED_BASELINE \
  --target-os debian --target-os-version 12
```

Use `ubuntu` and its actual version for an Ubuntu target. The export accepts
only SDK headers/libraries and compiler tools; absolute/escaping symlinks are
rejected. A normal host installation is not automatically a portable cross SDK.
Review and pin the resulting `sdk-lock.json` hash before configuring a build.
The example does not declare Debian 12 supported on A133.

## Signed bundle in a deb container

After building and signing with the existing release pipeline:

```sh
python3 tools/sdkbuild/package_deb.py --profile PROFILE.json --sdk SDK_DIR \
  --bundle SIGNED_BUNDLE_DIR --verifier TRUSTED_HOST_MIMI_UPDATE \
  --trust PLATFORM_TRUST.json --device REVIEWED_TARGET.json \
  --epoch SOURCE_DATE_EPOCH --maintainer 'Actual maintainer <actual-email>' \
  --output NEW_DEB_OUTPUT
```

The verifier is the host build of the existing `mimi-update` checker. The tool
copies and verifies exactly the archive, manifest and detached signature it
will package; it does not create or import signing keys. It binds the signed
build receipt to the selected SDK, source commit, OS version, screen list and
voice option. Every signed target must require the exact capability
`target-os:<debian-or-ubuntu>:<VERSION_ID>`. That capability must come from the
controlled platform identity; the application bundle must never add it to a
device. Tina, system bundles, wrong targets and missing/mismatched evidence fail.

The result is `smartdesktopmimi-bundle-a133_VERSION_arm64.deb` plus
`deb-build.json` recording the outer deb hash and inner signed application hash.
Prereleases use Debian's `~` ordering. Fixed source epochs and root ownership
produce repeatable archives. The package copies only the signed bundle, matched
profile and instructions under `/usr/share`; it contains no maintainer scripts,
keys, SDK contents, Provider or system service replacements.

**Installing this deb stages a candidate; it does not activate the desktop.**
The installed instructions request activation through the platform-owned
`mimi-update install-offline` command. That existing updater owns trust,
hardware/OS/ABI matching, channel/replay policy, mutual exclusion, health checks,
rollback and persistent data. Its hooks must first be integrated and tested on
the Debian/Ubuntu platform. No such qualified platform is currently supplied.
Removing/purging the transport deb removes its staging copy, not updater-managed
installed versions or user data. dpkg package status and active desktop version
are intentionally recorded separately; do not report an unpacked bundle as an
installed/healthy application.

The outer deb needs authenticated distribution or a separately verified release
hash; the inner signature does not authenticate arbitrary dpkg metadata.
Release collection/upload integration and real installation/rollback/powercut
tests remain pending. Host fixture tests verify archive structure and rejection
behavior only; their stub verifier is not cryptographic or hardware acceptance.
The packaging lifecycle follows the distinction between unpacking and configuring
in the [Debian maintainer-script policy](https://www.debian.org/doc/debian-policy/ch-maintainerscripts.html).
