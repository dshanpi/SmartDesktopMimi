# System architecture

## Design rule

Dependencies point inward: frontends and service transports invoke application
use cases; use cases depend on domain policy and ports; only platform providers
and integrations depend on a vendor SDK. A UI, domain object or generic service
must not name an SoC or open a hardware device node.

```text
frontends (LVGL/Web/CLI)
          |
application use cases <--- IPC v1 compatibility adapter
          |
domain policy + typed events
          |
core/ports + IPC v2 contracts
          |
platform providers and external integrations
```

## Runtime boundaries

The target deployment is intentionally hybrid. `aitvbox-core` coordinates
system state. Capture, input, agent, apps and update use independent processes
because they own conflicting resources, process untrusted input, or need an
isolated failure boundary. Wi-Fi, Bluetooth, sensor and cloud logic may remain
logical modules in core until isolation produces a measurable benefit.

The current migration milestone supplies the stable port ABI, platform
providers, manifests, architecture dependency gate and an additive IPC V2
server. Existing V1 callers continue to work. A133 capture and HID ownership
still use the validated legacy process/lock boundaries; moving them behind one
daemon is the next hardware-gated milestone and is not claimed complete.

Capture has exclusive ownership of the HDMI input. Its local-display, JPEG and
H264/WebRTC consumers share a single validated frame policy. On A133 the final
eight LT6911/VIN rows are invalid: 1072 Y rows are resampled to 1080 and 536 UV
rows to 540. Copying row 1071 is forbidden.

Input has exclusive ownership of production `/dev/hidg0` keyboard and
`/dev/hidg1` relative mouse devices. Production reports have no report IDs;
composite report IDs are service-mode fallback only. Every exit and error path
releases all keys and buttons.

IPKVM remains a separately built and packaged GPL-2.0 process. It consumes only
documented control/media sockets and is never statically linked into the
product core.

## Protocol evolution

V1 is the legacy 16-byte native C header with a 512-byte payload. V2 uses a
four-byte big-endian length and a JSON envelope on
`/run/aitvbox/backend-v2.sock`; media stays binary. V1 remains operational for
two public releases while callers move topic-by-topic. Each migrated command
requires V1/V2 parity tests. Unknown fields are forward-compatible, whereas an
unknown version/topic produces a structured error.

## Platform contract

`core/ports/aitvbox_ports.h` is the stable C ABI. Exactly one statically linked
provider supplies a descriptor and supported ports. Capability-driven callers
must return `AITVBOX_UNSUPPORTED` for absent optional functions. Platform
manifests are the source of truth for SDK discovery, architecture, build,
packaging, flash and validation metadata.
