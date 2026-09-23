# Test matrix

## Host gates

The public-source gate needs no Tina SDK or restricted provider. It runs the
architecture and host integration tests, Go tests with the race detector, the
device UI build and targeted lint, the release audit, and deterministic SBOM
generation:

```bash
python3 tools/aitvbox.py test --platform a133 --scope public
```

The same command runs in `.github/workflows/public-ci.yml` with Python 3.11,
Go from `go.mod`, and Node 22.21.1. GitHub Actions are pinned by full commit
SHA. In a history-free export the command automatically selects candidate
mode, which rejects every path declared for exclusion.

```bash
python3 -m unittest tests.architecture.test_architecture
python3 scripts/check_architecture.py
python3 -m unittest tests.platform.test_host_integration
(cd apps/ipkvm/upstream && /usr/local/go/bin/go test ./...)
(cd apps/ipkvm/upstream && /usr/local/go/bin/go test -race ./...)
(cd apps/ipkvm/upstream/ui && npm ci && npm run build:device && npm run lint:public)
python3 tools/public_release_audit.py --candidate  # exported repository only
```

Factory-assistant Rust tests require a Cargo version that supports lockfile v4. An older toolchain is a failed prerequisite, not a skipped pass. Every touched A133 C/C++ component is cross-compiled. IPC tests cover split/coalesced frames, invalid JSON, zero/oversized lengths, envelope validation, forward-compatible fields, peer rejection and reconnect. Platform tests compile fake providers and reject vendor dependencies outside adapters.

## A133 board gate

After a full gated flash, verify boot/partitions and processes; then USB gadget self-test, UDC configured state, split HID reports and released keys. Capture a board JPEG and browser `--video-only` 1920x1080 image of the same taskbar. Validate bottom rows, WebRTC reconnect and capture ownership.

Run safe Agent observation-only before HID. Every action needs a fresh HDMI frame; block the third identical ineffective action. Test 250 ms key hold, Meta/blur/visibility reset, emergency stop and crash release. Verify Wi-Fi, Bluetooth, sensors, LED 25 Hz/no-duplicate behavior, 50% initial volume, `你好涂鸦`, TTS leakage rejection, deliberate barge-in, app sandbox, strict-TLS cloud, signed OTA success and forced rollback.

Serial access uses the serial agent. Temporary SSH/staging is removed, authentication restored by hash, and HID released before success. Reports record revision/dirty state, platform, tools, commands, counts, hashes, board alias, evidence, limitations and rollback commit. Hardware-unavailable results are `NOT_RUN`, never `PASS`.
