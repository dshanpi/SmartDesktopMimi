---
name: aitvbox-debug-hdmi-hid
description: Safely diagnose, repair, deploy, and validate the Smart Desktop Mimi A133 HDMI capture, browser IPKVM/WebRTC, AI HDMI MCP, USB HID, Tuya free-chat audio, Wi-Fi, Bluetooth, and WS2812 stack. Use for HDMI artifacts, browser-versus-raw capture comparisons, input routing, Agent visibility, interrupted or self-triggered speech, cloud binding, board deployment, recovery, or source backup work in AI-DeskTopBox.
---

# AITVBox HDMI/HID Debugging

Treat this as a fragile production workflow. Preserve the known-good source and
authentication state, identify the failing layer with evidence, then validate
the real browser-to-board-to-Windows path.

## Start safely

1. Work from `/home/ubuntu/AI-DeskTopBox` unless the active workspace says
   otherwise.
2. Inspect `git status`, the current branch, submodules, disk space, and board
   reachability before changing anything.
3. Before a risky change, run from the repository root:

   ```bash
   .codex/skills/aitvbox-debug-hdmi-hid/scripts/backup_source.sh \
     /home/ubuntu/AI-DeskTopBox
   ```

   Require both archive readability and SHA-256 output.
4. Preserve user changes. Never reset, clean, or discard a dirty tree.
5. Read [references/known-good-baseline.md](references/known-good-baseline.md)
   before changing video, USB gadget, HID reports, browser input, board
   authentication, or deployment files.

## Interpret the target correctly

- Treat the HDMI frame visible at `http://192.168.1.44/` as the only target
  display.
- Do not decide success from Windows “main display” or “secondary display”
  labels.
- Require the requested result to be visible in the current browser HDMI
  frame.
- If a window is on another Windows display, activate it and use
  `Win+Shift+Left/Right` until it appears in this frame.
- Distinguish VMware input capture from HID failure. Release VMware capture
  before diagnosing the USB device.

## Use board access safely

- Access `/dev/ttyUSB1` only through:

  ```bash
  python3 /home/ubuntu/A133-Tina5.0-v0.9/tools/serial_agent/serial_agent_client.py \
    --sock /tmp/a133-serial.sock
  ```

- Never open the serial device directly.
- Prefer the browser, MCP, or serial agent for diagnostics.
- If SSH is temporarily required, add one task-labelled key through the serial
  agent, keep it under `/tmp`, and remove the authorized-key line and local key
  before finishing.
- Never expose or overwrite the model API key.
- If authentication must be changed temporarily for a browser test, first
  save the exact config, restore it byte-for-byte, verify its SHA-256, restart
  IPKVM, and remove every temporary credential.

## Locate the failing layer

1. Capture a board-side JPEG and a browser WebRTC screenshot of the same HDMI
   state.
2. Compare the artifact:
   - Present in both: investigate VIN/NV12 reconstruction or encoder input.
   - Raw clean, browser corrupt: investigate H.264 profile, stride, WebRTC, or
     CSS/video sizing.
   - Browser frame clean, user input wrong: investigate focus, pointer lock,
     VMware capture, HID transport, and gadget enumeration.
3. Record WebRTC video dimensions, decoded/dropped frames, packet loss,
   freezes, and frame-rectangle stability.
4. Validate USB on the board:

   ```text
   UDC state=configured
   /dev/hidg0 keyboard ready
   /dev/hidg1 relative mouse ready
   ```

5. Do not claim a fix from service logs alone. Reproduce through the actual
   browser HDMI view and Windows host.
6. Do not infer VMware capture from a successful HID write followed by an
   unchanged frame. First test a held, reversible global shortcut. A short
   press/release can be dropped even while CapsLock LED synchronization works.

## Preserve the known fixes

- Do not replace the final eight invalid LT6911/VIN rows by copying one source
  row. Resample the 1072 valid rows over 1080; copying the last valid row turns
  Windows taskbar indicator colours into vertical bars.
- Keep production keyboard and mouse separate:
  `/dev/hidg0` keyboard and `/dev/hidg1` relative mouse.
- Keep composite Report IDs confined to service-mode fallback. Do not apply
  them to separate production devices.
- Preserve the Linux 4.9 `f_hid` handling for Windows
  `GET_PROTOCOL`, `SET_PROTOCOL`, `SET_REPORT`, and HID descriptor requests.
- Preserve the production USB identity/revision used to invalidate stale
  Windows descriptor caches.
- Preserve browser keyboard reset on Meta shortcuts, blur, visibility change,
  and lost input focus. A missed Meta `keyup` must not leave Win held down.
- Keep autonomous Agent keyboard reports pressed for at least 250 ms and below
  the Windows repeat threshold. A successful `/dev/hidg0` write proves only
  transport acceptance, not execution by Windows.
- Require post-action frame verification. Never execute the same ineffective
  HID action more than twice consecutively; the third identical action must be
  blocked and surfaced as a recovery event.
- Prompt instructions are not a screen-routing security boundary. For a
  NetEase task, the action executor must reject search, completion, generic
  hotkeys, typing, mouse input, and scrolling unless the current model
  observation affirmatively identifies NetEase in the current HDMI frame.
- `netease_search` is an in-window operation only: it focuses search, replaces
  the query with printable ASCII/pinyin, and submits it after NetEase is
  visibly proven. It must never use `Win+D`, `Win+T`, `Win+R`, or another
  global focus guess. Hard-limit it to two executions for the whole task.
- If NetEase is absent, permit only bounded `window_cycle`, then at most one
  armed `move_active_window left|right`, followed by a fresh frame. Moving a
  window is legal only after a successful cycle selected it. Rediscover the
  direction after a Windows reboot or display-topology change; never hard-code
  historical left/right placement. If the target cannot be made visible,
  fail closed instead of operating another display or closing unrelated
  windows.
- During a controlled diagnostic only, a reversible global shortcut such as
  `Win+D` can help prove that the HID host and HDMI source match. It is not an
  autonomous application-focus strategy. A dialog absent from the HDMI frame
  may merely be on another display. A CapsLock LED output report proves that a
  host consumed the keyboard report, but does not prove that the focused
  window is on the captured display.
- Never convert a coordinate from the 1920×1080 HDMI frame directly into a
  Windows desktop coordinate. Per-monitor DPI scaling changes the logical
  bounds used by Windows automation. The 2026-07-31 host reported primary
  `DISPLAY1={X=0,Y=0,Width=2048,Height=1152}` and captured
  `DISPLAY2={X=2048,Y=0,Width=1280,Height=720}` while HDMI remained
  1920×1080, proving 150% scaling on the captured display. Rediscover the
  bounds after display changes instead of hard-coding this historical value.
- With the production relative mouse, expose the cursor with a harmless slow
  move and capture a fresh frame before clicking. Calibrate from the visible
  cursor to the visible target and use only the local delta. Do not anchor at
  a virtual-desktop edge and assume one HID unit equals one capture pixel.
  `tools/board/hid_relative_slow.c` exists for this bounded recovery.
- Do not use hidden `Win+R`/PowerShell automation as a substitute for visible
  HDMI interaction. Run-dialog focus can stay on the uncaptured primary
  display, and an unchanged HDMI frame gives no evidence that the command was
  typed or executed.
- For music playback, a search page, highlighted row, or large Play button is
  not completion. Require a direct frame showing the requested artist in the
  bottom player and pause bars, or two direct frames showing progress
  advancement. Tiny model OCR is advisory only. If NetEase opens a VIP trial
  dialog, `ESC` is a safe dismissal only after playback is proven and must be
  followed by another playback-state frame.
- Keep the AI prompt scoped to the current HDMI frame and expose concise
  `observation`, `decision`, `action`, and tool/HID result entries. Do not
  expose hidden chain-of-thought.

## Build and deploy

Before changing the AI desktop runtime, preserve these product invariants:

- New devices start at 50% speaker volume. Do not silently restore the older
  80% default; excessive output can destabilize the board and worsens acoustic
  self-triggering.
- The current A133 offline MNN wake model supports `你好涂鸦`. Do not advertise
  `Hey Tuya` until a matching English wake model has been obtained and tested.
- Free-chat must reject speaker leakage as barge-in while still accepting
  deliberate external speech. Validate both directions with real TTS playback.
- Tuya UUID/AuthKey/SN are factory data. Load them from the protected runtime
  license and never commit them to source, logs, screenshots, or firmware.
- Keep WS2812 rendering bounded at 25 Hz and suppress duplicate frames; an idle
  state must not continually rewrite the strip.
- Never run the Tina SDK `./build.sh` without an explicit action. Before any
  flash, require a successful pack and `scripts/a133-bootchain-gate.sh IMAGE`.

Build the touched components before deployment:

```bash
make -C apps/hdmi_preview \
  SDK_ROOT=/home/ubuntu/A133-Tina5.0-v0.9 -j"$(nproc)"
make -C apps/platform_services \
  SDK_ROOT=/home/ubuntu/A133-Tina5.0-v0.9 -j"$(nproc)"
make -C apps/ipkvm \
  SDK_ROOT=/home/ubuntu/A133-Tina5.0-v0.9 \
  BUILD_DIR=/home/ubuntu/AI-DeskTopBox/build/ipkvm -j"$(nproc)"
```

Before replacing board binaries:

1. Copy the current board binaries to the dated local backup directory.
2. Upload replacements to `/tmp`.
3. Stop IPKVM before overwriting its running binaries.
4. Replace only the named files, set mode `0755`, sync, verify hashes, and
   restart.
5. Remember that `/overlay` may be 97% full. Use `/tmp` for staging and keep a
   local recovery copy instead of duplicating 20 MB binaries on overlay.

## Validate proportionally

Run all applicable checks:

```bash
python3 -m unittest tests.platform.test_host_integration
(cd apps/ipkvm/upstream && /usr/local/go/bin/go test ./...)
(cd apps/ipkvm/upstream/ui && npm run build:device)
```

Also require:

- A133 cross-compilation of every changed C/C++ component.
- Board raw snapshot with a clean bottom edge.
- Browser `--video-only` smoke test at 1920×1080.
- Browser screenshot showing the clean taskbar.
- Safe AI observation-only task showing real observation text and no HID
  result.
- USB gadget self-test, UDC `configured`, services alive, and released HID
  keys/buttons.

## Back up remotely

- Use the active `feature/a133-smart-desktop-*` branch for a recoverable product
  snapshot; preserve `supportappconfig` as its historical base.
- Commit and push a dirty submodule first, then update and push the main
  repository pointer.
- Verify remote refs with `git ls-remote`.
- Commit curated schematics, maintenance notes, and validation screenshots when
  they explain a hardware invariant. Do not commit raw framebuffer dumps,
  archives containing build outputs or nested `.git`, or device credentials.
- Do not report success until the remote ref equals the intended local commit.

## Clean up

Before finishing:

- Stop any unfinished Agent task and release keyboard/mouse reports.
- Restore the original IPKVM authentication config and verify its hash.
- Remove temporary SSH authorization, uploaded helpers, credentials, and
  staging binaries.
- Verify cleanup through the serial agent after SSH removal.
- Report backup paths and hashes, remote commits, tests, board status, and any
  deliberately excluded non-source files.
