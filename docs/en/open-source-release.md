# Public/private release boundary

Create public releases from an audited snapshot in a new repository. Never push or rewrite this development repository's secret-bearing history and call it clean.

Public: product source, contracts, tests, build tooling, redistributable integration code, documentation, license and generated SBOM. Private `AI-DeskTopBox-ops`: factory databases, per-device licenses, signing/private keys, release credentials, customer configuration, restricted SDKs and provisioning operations.

Before export, rotate historical production trust material, classify every binary/vendor input, remove generated outputs, scan content and Git objects, review third-party notices and inspect the candidate file list. Initialize new history only from that candidate. Clone it into an empty directory and repeat doctor, tests and reproducible build before publishing.

The fail-closed commands are:

```bash
python3 tools/public_release_audit.py
python3 tools/aitvbox.py test --platform a133 --scope public
python3 tools/export_public_source.py DESTINATION \
  --acknowledge-rotated-secrets \
  --acknowledge-licenses-reviewed
```

The exporter requires a clean committed tree. It creates a new single-commit
repository, materializes the exact reviewed LVGL revision instead of retaining
the development repository's private submodule URL, applies the reviewed A133
export policy from `docs/public-release.json`, generates SPDX and CycloneDX
SBOMs plus `release/SHA256SUMS`, and writes a reproducible `.tar.gz` and
checksum beside the destination. `release/excluded-content.json` records every
removed path and byte count; `release/source-size.json` records the resulting
tree size. The exporter never adds a remote or pushes.

The audit covers the Git index and every new non-ignored file, so newly added
content cannot bypass the gate. Downloaded TuyaOpen tools and build trees,
restricted A133 vendor libraries, the external 100ask SDK and private keys, factory licenses and
databases, and local validation artifacts may remain available to an internal
build on disk, but they must stay untracked under the root `.gitignore`. Never
force-add those paths to a public candidate.

Documentation PDFs and images intended for publication are classified in
`docs/public-assets.json` with their source, GPL-3.0-or-later license and
SHA-256. Raw framebuffers, browser captures under `output/`, firmware images,
archives and unclassified large files are blocked. The audit reports only a
path and rule identifier; it does not echo matched credential material.

Third-party placement is not an allowlist. Every retained binary asset must
have a file-level source, SPDX license, license file and SHA-256 entry in
`docs/public-release.json`. The A133 profile excludes the netboot ISO,
the legacy IPKVM ARM executable and CircularXX webfonts, non-A133 prebuilt
audio libraries, optional Tuya applications/display engines, and upstream
demo/test corpora. Audit an exported repository with
`python3 tools/public_release_audit.py --candidate`; a declared exclusion that
survives export is a blocking error.

The public build uses a no-network cloud stub. Internal builds enable the
external provider only with both `AITVBOX_ENABLE_100ASK_CLOUD=ON` and an
absolute `AITVBOX_100ASK_SDK_ROOT`; directory auto-detection is intentionally
unsupported.

Product-owned code is GPL-3.0-or-later or commercially licensed by 09make.inc. The CLA grants relicensing rights. Third-party licensing is unchanged; IPKVM stays a separate GPL-2.0 package/process.
