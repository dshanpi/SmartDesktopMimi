# AI-DeskTopBox / Smart Desktop Mimi

[中文（默认）](README.md) | English

AI-DeskTopBox is an open, layered embedded AI desktop and IPKVM platform from
09make.inc (零九智造). A133/B6 is the reference implementation. A527, V883,
RK3576 and RV1106 currently provide fail-closed port skeletons; they are not
claimed as supported until their vendor SDK builds and physical board tests
pass.

![Smart Desktop Mimi](docs/solution/assets/smart-desktop-mimi-hero.png)

## Highlights

- A 1024×768 LVGL desktop with touch-friendly navigation, local applications,
  device status, settings and update flows.
- Tuya AI free chat with per-device credentials injected at provisioning time,
  never embedded in the generic firmware or public source.
- HDMI preview, browser WebRTC IPKVM and USB keyboard/mouse HID control.
- Wi-Fi, Bluetooth audio, WS2812 status lighting and signed A/B OTA support.
- A layered platform architecture with versioned contracts, testable provider
  boundaries and a fail-closed public-source export pipeline.

## Screenshots

The first image is the implemented home-screen design target. The following
images are direct 1024×768 A133 validation captures from the device UI.

![Smart Desktop Mimi home-screen design](desktop-ui-concept-v1.png)

| Desktop | Applications |
| --- | --- |
| ![A133 desktop](docs/debug-evidence/a133-en-us-20260906/final/desktop-r3.png) | ![Application launcher](docs/debug-evidence/a133-en-us-20260906/final/all-apps.png) |
| AI free chat | HDMI MCP |
| ![Tuya AI free chat](docs/debug-evidence/a133-en-us-20260906/final/ai-chat.png) | ![HDMI MCP](docs/debug-evidence/a133-en-us-20260906/hdmi-mcp.png) |

More product visuals and the solution brief are available in the
[Smart Desktop Mimi solution kit](docs/solution/README.md).

## Architecture and call-flow diagrams

The repository includes nine Chinese engineering diagrams covering the product
system, source modules, runtime processes, boot sequence, UI/backend, AI/Tuya,
HDMI/IPKVM/HID/Agent, user applications and cloud OTA. Editable Mermaid sources,
SVGs and high-resolution PNGs are provided.

[Browse the complete architecture and call-flow diagram set](docs/architecture-diagrams/README.md).

![Smart Desktop Mimi source-module architecture](docs/architecture-diagrams/rendered/02-source-module-architecture.png)

## Architecture

The system separates product use cases from hardware and vendor SDKs:

```text
LVGL / Web / CLI
       |
application use cases and versioned contracts
       |
domain policy and resource ownership
       |
Capture / Input / Audio / Network / Update ports
       |
A133 | A527 | V883 | RK3576 | RV1106 providers
```

The target resource and security boundaries use independent services. This
milestone installs the contracts and compatibility path; capture/HID daemon
ownership is still hardware-gated migration work. IPKVM remains a separately
packaged GPL-2.0 process. See [Architecture](docs/en/architecture.md) and the
[IPC v2 contract](core/contracts/v2/README.md).

## Build and test

The standard-library-only build frontend diagnoses the SDK before invoking a
vendor build:

```bash
python3 tools/aitvbox.py doctor --platform a133
python3 tools/aitvbox.py configure --platform a133 --config release
python3 tools/aitvbox.py test --platform a133 --scope architecture
python3 tools/aitvbox.py test --platform a133 --scope public
python3 tools/aitvbox.py build --platform a133 --clean
python3 tools/aitvbox.py package --platform a133 --check-only
```

Read [Build and release](docs/en/build-and-release.md),
[Porting guide](docs/en/porting.md), and [Test matrix](docs/en/testing.md)
before changing a platform provider or flashing a board.

## Public source release

Public releases are clean, history-free snapshots rather than rewrites of the
development repository. The exporter embeds the reviewed LVGL revision as
ordinary source, generates SPDX/CycloneDX SBOMs and file hashes, initializes a
single fresh commit, removes non-A133 demos, test corpora and unlicensed
prebuilt artifacts through a fail-closed file-level policy, and creates a
reproducible source archive locally. It does not configure a remote or push. See the
[public/private release boundary](docs/en/open-source-release.md).

## Platform status

| Platform | Status | SDK variable |
| --- | --- | --- |
| Allwinner A133/B6 | Reference implementation | `AITVBOX_A133_SDK` |
| Allwinner A527 | Adapter skeleton | `AITVBOX_A527_SDK` |
| V883 | Adapter skeleton | `AITVBOX_V883_SDK` |
| Rockchip RK3576 | Adapter skeleton | `AITVBOX_RK3576_SDK` |
| Rockchip RV1106 | Adapter skeleton | `AITVBOX_RV1106_SDK` |

## Licensing and security

Product-owned code is offered under GPL-3.0-or-later or a commercial license
from 09make.inc. Individual third-party components keep their own licenses;
notably `apps/ipkvm/upstream` is GPL-2.0 and is isolated as a process/package.
Contributions require the project CLA. Never commit device credentials,
factory licenses, signing keys, SDKs, or release secrets. See
[SECURITY.md](SECURITY.md), [CONTRIBUTING.md](CONTRIBUTING.md), and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
