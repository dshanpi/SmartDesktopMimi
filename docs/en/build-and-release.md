# Build and release

## Host preparation

Install Git, Python 3, Make, CMake, rsync, squashfs-tools, e2fsprogs and the
vendor SDK dependencies. Initialize the LVGL submodule. Keep SDKs outside this
repository; builds borrow only their toolchain and sysroot.

```bash
git submodule update --init --recursive
export AITVBOX_A133_SDK=/home/ubuntu/A133-Tina5.0-v0.9
python3 tools/aitvbox.py doctor --platform a133
```

`doctor` checks SDK markers and repository inputs. A skeleton platform exits
with code 2 until its SDK and implementation have been validated.

## A133 workflow

```bash
python3 tools/aitvbox.py configure --platform a133 --config release
python3 tools/aitvbox.py test --platform a133 --scope architecture
python3 tools/aitvbox.py build --platform a133 --clean
python3 tools/aitvbox.py test --platform a133 --scope all
python3 tools/aitvbox.py package --platform a133 --check-only
python3 tools/aitvbox.py package --platform a133
```

Configuration metadata is written to `out/a133/release`; application and
release artifacts continue to use the existing `build/` layout during
migration. A formal release rejects a dirty tracked tree unless
`--allow-dirty` is explicitly recorded in its manifest.

Before flashing, verify `BUILD-COMPLETE` and `SHA256SUMS`, then run:

```bash
python3 tools/aitvbox.py flash --platform a133 \
  --image build/releases/<release>/firmware/a133_linux_b6_uart0.img
```

The command authenticates and runs the board-verified boot-chain gate. It does
not automate PhoenixSuit/LiveSuit: an operator selects the designated test
board and image, performs the full flash, then executes the board checklist.
Never call the Tina `build.sh` without an explicit action.

## Reproducibility

A release records the Git revision, dirty state, SDK/toolchain identity,
component hashes, image hash, commands and test report. SDK configuration must
be restored after success or failure. Only a fresh-clone build of the audited
public snapshot is eligible for a public tag.
