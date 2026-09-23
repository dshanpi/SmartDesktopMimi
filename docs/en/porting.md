# Hardware porting guide

## Port lifecycle

1. Obtain a legally usable SDK, board, schematic, boot/recovery procedure and serial voltage information. Record exact versions and checksums privately.
2. Complete `platforms/<id>/platform.json`. Keep status `skeleton` and all capabilities empty until each implementation is tested.
3. Implement the toolchain/sysroot mapping without absolute paths in CMake; use the manifest environment variable for local SDK discovery.
4. Implement one provider for `core/ports/aitvbox_ports.h`. Vendor includes, memory layouts, device nodes and SoC macros stay in this platform directory.
5. Add kernel/DTS/rootfs overlays and deterministic packaging. Preserve a recovery path and add a boot-chain gate appropriate for that vendor.
6. Implement capabilities one at a time. Each capability needs host contract tests, cross-compilation and a physical test before it is advertised.
7. Complete the validation manifest, repeat a clean build/pack/flash, archive evidence, then change status from `skeleton` to `validated`.

## Capability checklist

- Display: resolution, format, stride, rotation, backlight and failure state.
- Capture: signal detection, buffer ownership, cache coherency, JPEG and H264 consumers, unplug/replug and long-run stability.
- Input: descriptor, report format, endpoint ownership, host enumeration, emergency release and service fallback.
- Audio: capture/playback devices, sample-rate conversion, default 50% volume, echo/self-trigger rejection and external barge-in.
- Network/Bluetooth: persistent identity, reconnect, coexistence and failure recovery without cross-layer callbacks.
- Sensor/LED: calibrated units, absent-device behavior, 25 Hz LED limit and duplicate suppression.
- Update: partition map, inactive-slot write, signed metadata, power loss, health commit and rollback.
- Identity: factory data from protected runtime storage, never source or logs.

A133 is the reference. A527 must not assume A133 multimedia APIs. V883 needs its vendor media and packaging model identified. RK3576 vendor MPP/RGA, DTS and loader integration belong only in its provider. RV1106 is 32-bit ARM in the current manifest, so memory and ABI assumptions require a fresh audit.
