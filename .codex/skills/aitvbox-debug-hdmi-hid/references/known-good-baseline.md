# Known-good AITVBox HDMI/HID baseline

## Scope

Use this reference when touching the A133 board HDMI capture, IPKVM WebRTC
sender, AI HDMI MCP, USB gadget, browser input, or deployment/authentication
flow.

## Known-good topology

- Product repository: `/home/ubuntu/AI-DeskTopBox`
- Tina SDK: `/home/ubuntu/A133-Tina5.0-v0.9`
- Board address: `192.168.1.44`
- Serial-agent socket: `/tmp/a133-serial.sock`
- Serial port owned by the agent: `/dev/ttyUSB1`
- Browser control surface: `http://192.168.1.44/`
- Source recovery branch: `supportappconfig`

Known board Wi-Fi provisioning (operator-supplied):

- SSID: `Programmers7`
- Passphrase: `100asktech`
- Use these values when the A133/R818 board needs network recovery; do not ask
  the operator to repeat them.

Production USB state:

- USB product: `18d1:d015`
- `bcdDevice`: `0x0428`
- UDC: `5100000.udc-controller`
- UDC state: `configured`
- Keyboard: `/dev/hidg0`, eight-byte boot-keyboard report, no Report ID
- Mouse: `/dev/hidg1`, four-byte relative report, no Report ID

Service-mode composite fallback:

- Report ID 1: relative mouse
- Report ID 2: keyboard
- Do not use these IDs for the separate production nodes.

## HDMI bottom-row lesson

The LT6911/VIN path reports 1920×1080 but its final eight delivered NV12 rows
are invalid. A previous workaround copied source row 1071 into rows 1072–1079.
That hid the green tail, but when the Windows taskbar occupied the bottom edge,
the coloured running-app indicators in row 1071 became vertical colour bars.

Correct reconstruction:

- Y: map 1072 valid source rows across 1080 destination rows.
- UV: map 536 valid source rows across 540 destination rows.
- Apply the same policy to the H.264 encoder input and JPEG snapshot path.

Key source locations:

- `apps/ipkvm/video/main.cpp`
- `apps/hdmi_preview/src/main.cpp`

## HID failure lessons

The keyboard/mouse incident was a stack of independent issues:

1. Older configuration could point keyboard and mouse at the same `hidg0`,
   while the browser/backend assumed separate reports.
2. Windows requires HID class/control requests that the legacy Linux 4.9
   `f_hid` implementation did not fully handle.
3. Windows caches descriptors by USB identity. Descriptor changes without a
   product/revision bump can leave the old interpretation active after a
   service restart.
4. Chromium may suppress a normal `keyup` during Meta/Win shortcuts. Replaying
   stale modifier state leaves Win logically held on the target.
5. VMware can capture input independently of USB correctness. If the VMware
   status says input is directed to the guest, release it before testing the
   Windows host.
6. An immediate press/release can be accepted by `/dev/hidg0` yet missed by a
   loaded Windows host. CapsLock may still work because lock-key output reports
   take a different path. Autonomous Agent key reports must remain pressed for
   at least 250 ms, then explicitly release all eight bytes.

Do not diagnose VMware or cross-wiring from an unchanged frame until a held,
reversible global shortcut has been tested. On 2026-07-31, a one-second
`Win+D` changed the captured HDMI frame immediately while short reports did
not, proving that USB and HDMI were on the same Windows machine and that report
duration was the actual fault.

Agent policy:

- Verify the next HDMI frame after every HID action.
- Block the third consecutive identical action.
- Treat model prompts as guidance, not enforcement. For NetEase tasks, the C
  action executor must require an affirmative current-frame observation of a
  visible NetEase window before accepting search, typing, mouse, scrolling,
  generic hotkeys, or completion.
- Use `netease_search` only inside an already visible NetEase window. It may
  focus and replace the search query, but may not double-toggle `Win+D` or use
  `Win+T`/`Win+R` as an invisible focus guess.
- Permit at most two `netease_search` executions in the whole task, including
  when unrelated actions occur between attempts; never burn the step budget
  on repeated TAB guesses through invisible focus states.
- When NetEase is absent, allow only bounded `window_cycle`, followed by one
  armed `move_active_window left|right` and a new HDMI frame. Reject movement
  without a preceding cycle, reject generic `Alt+F4`/Escape and other hotkeys,
  and never close an unrelated visible window. Do not assume the target is
  historically left or right: reboot and display re-enumeration can change
  topology. Stop safely if the application cannot be brought into the frame.
- A CapsLock LED output report proves host-side HID consumption, not that the
  focused window is visible on the HDMI capture.
- Do not map 1920×1080 capture pixels directly to Windows virtual-desktop
  coordinates. The validated 2026-07-31 topology was:
  `DISPLAY1 primary {X=0,Y=0,Width=2048,Height=1152}` and
  `DISPLAY2 captured {X=2048,Y=0,Width=1280,Height=720}`. The captured HDMI
  frame stayed 1920×1080, so DISPLAY2 used 150% DPI scaling. Treat these
  values as diagnostic history and re-enumerate after a topology change.
- For relative-pointer recovery, first make the cursor visible in a fresh
  frame. Move from that visible cursor by a small calibrated delta to the
  visible target. Edge anchoring plus assumed pixel ratios is not sufficient.
  The source helper is `tools/board/hid_relative_slow.c`.
- Hidden Run-dialog or PowerShell actions on the primary display are not
  evidence of success and are not a supported replacement for visible HDMI
  control.
- NetEase playback is proven only by a bottom-player artist match plus pause
  bars, or by visible progress advancement across direct frames. Search
  results and the large Play button are intermediate state. Small model OCR
  can hallucinate a track name and must not override the direct frame.

Key source locations:

- `packaging/aitvbox-usb-hid/files/aitvbox-usb-hid.init`
- `packaging/aitvbox-usb-hid/files/aitvbox-hidctl`
- `integrations/usb-hid/linux-4.9-f_hid-windows-control-requests.patch`
- `apps/ipkvm/upstream/platform_hid_a133.go`
- `apps/ipkvm/upstream/ui/src/layout/core/desktop/hooks/useKeyboardEvents.ts`
- `apps/ipkvm/upstream/ui/src/layout/core/desktop/hooks/usePointerLock.ts`

## AI visibility and display semantics

The browser AI workspace must retain the live HDMI view while showing ordered
events:

1. task
2. capture
3. thinking/waiting
4. concise visible-screen observation
5. concise action reason
6. compact action JSON
7. HID/tool result
8. verified completion or failure

Do not expose private chain-of-thought. A concise observation and action reason
are sufficient.

The browser HDMI frame is the target. “Main display” and “secondary display”
are not reliable control concepts. A task is complete only when its result is
visible in the current frame.

### 2026-07-31 wrong-screen recurrence

The model repeatedly reported that NetEase was absent from the current HDMI
frame, but the old executor still accepted `netease_search`, `Win+T`, `Win+D`,
and later `Alt+F4`. Its deterministic search routine used a double `Win+D`
focus guess, so Ctrl+F could land in an invisible window; one run subsequently
surfaced and manipulated a `BillBoard Device` properties window. The USB HID
transport and pointer scaling were not the primary fault. The missing boundary
was semantic validation between the model response and the HID executor.

The corrected executor now fails closed. It admits only bounded window
inspection while the requested application is absent, requires every move to
be armed by a successful cycle, checks the next captured frame, and requires
visible playback evidence before accepting completion. A prompt-only rule is
not an acceptable substitute for this gate.

On 2026-07-31, the validated recovery for “play a Xu Liang song” used a visible
cursor calibration on the captured frame. The final direct HDMI frame showed a
Xu Liang track in the bottom player and the central pause bars. NetEase's VIP
trial dialog was dismissed with `ESC`, after which playback remained visible.

## 2026-09-07 Smart Desktop Mimi AI baseline

- Validated firmware: `a133_linux_b6_uart0.img`
- Firmware SHA-256:
  `7996710c0420d1fca5ef2bec810cd244fd0cdbac6cf52d9d312d90d00f3bbd57`
- Validated DHCP address: `192.168.1.62` (diagnostic only; rediscover it after
  reconnecting because the address is not static).
- Tuya binding completed, MQTT connected, device-online DP reported, volume DP
  acknowledged at 50%, and metadata reporting succeeded.
- English speech was recognized and produced a complete 17-second response.
  Playback returned from SPEAK to LISTEN with zero ALSA underruns and without
  accepting its own speaker output as barge-in.
- Deliberate external speech six seconds into TTS was accepted as barge-in and
  returned the state machine to LISTEN. A later 23-second response also played
  with zero underruns.
- The current offline wake-word model is Chinese and supports `你好涂鸦` only.
  `Hey Tuya` remains unavailable until the vendor supplies a compatible English
  A133 MNN model.
- WS2812 output is bounded to 25 Hz with duplicate frames suppressed.
- The executable boot chain must remain byte-identical to the 2026-09-04 golden
  baseline. Only kernel, DTS, rootfs, application, and pack changes are in the
  normal workflow.
- Device UUID/AuthKey/SN are intentionally absent from this record. Recover
  them from the protected factory license, never from Git.

## Recovery invariants

- Preserve unrelated dirty-tree changes.
- Preserve the exact original IPKVM auth config and hash.
- Never print or commit the model API key.
- Stage large board binaries in tmpfs, not overlay.
- Keep local copies of old board binaries before replacement.
- Remove temporary SSH keys from both host and board.
- End with released HID keys/buttons and no Agent lock.
- Verify through the serial agent after removing SSH access.
