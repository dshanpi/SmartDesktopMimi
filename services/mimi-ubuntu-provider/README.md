# Ubuntu session Provider foundation

Requirement: [REQ-MIMI-UBUNTU-022](../../docs/change-gates/REQ-MIMI-UBUNTU-022.md).
This is a separate platform process for the native Ubuntu development desktop.
It uses the frozen V1 protocol, observes local active IPv4 interfaces with
`getifaddrs`, and reports unsupported board/cloud operations without performing
them. An IPv4 address does not prove internet access or account ownership.
No firmware, credentials, network settings, host clock, audio mixer or device
nodes are read or modified by the service. The real OS interface query is not a
Wi-Fi scan; Ubuntu Wi-Fi/Bluetooth control, media, ASR, private 100ask and Tuya
Providers remain to be integrated and qualified. Their replies cannot report
successful hardware activation. Bluetooth's frozen V1 status has no error field;
it stays disabled, and capability-aware desktop presentation remains pending.

Build through `tools/sdkbuild/native_ubuntu.py`, which now records this separate
binary alongside the desktop. This service is **not part of the application
update payload**, and the development build receipt is not a trusted SDK or a
qualified Ubuntu platform identity.

Run the service under the desktop user with a private runtime directory (0700)
and a dedicated socket path, e.g. `$XDG_RUNTIME_DIR/mimi/ipc.sock` after creating
that directory. Start `mimi-ubuntu-provider --socket ABSOLUTE_SOCKET_PATH` and
point the desktop's config `ipc.socket_path` to that same path via
`LV_PORT_LINUX_CONFIG`. Keep `LV_SETTINGS_FILE` in a separate persistent user
configuration directory; never put settings in the transient runtime directory.
The caller owns the runtime directory lifecycle. Do not use the A133 /tmp
endpoint, run as root, or launch into another user's graphical session.

Only same-UID peers are accepted. The service requires a canonical, owned 0700
parent, a 0600 singleton lock and a 0600 socket. It rejects unexpected files,
symlinks and a second active instance. After a crash it can reclaim its own
socket once it holds the instance lock; it does not remove unrelated files.
SIGTERM/SIGINT clean up the owned endpoint. Invalid/oversize frames close only
the offending connection; partial frames expire after five seconds. At most
eight clients are retained, and inactive clients expire after twenty seconds.
UI heartbeats keep active sessions connected. Payload bodies are never logged.

Complete capability-aware desktop UX, session startup hooks,
trusted platform identity, Ubuntu SDK capture and signed amd64 deb installation,
upgrade, rollback and data-preservation acceptance are still pending. Do not
present this foundation or an SDL screenshot as completion of those gates.

## V2 query endpoint (REQ-MIMI-UBUNTU-024)

The service also owns `ABSOLUTE_SOCKET_PATH.v2` in the same private directory.
Both endpoints must be ready before startup succeeds. V2 uses the existing
4-byte big-endian length plus UTF-8 JSON envelope (schemaVersion 2, max64KiB).
Only same-UID requests are accepted. Send `system.ping` or `platform.describe`
with an empty object payload and a requestId. Responses echo the requestId.
Unknown topics return UNKNOWN_TOPIC; query arguments return INVALID_ARGUMENT.
Malformed envelopes, unsupported schemas, events/responses sent as requests,
invalid UTF-8 and framing errors disconnect only that peer. The service uses
json-c strict parsing and the shared V2 validator, with bounded receive/send
buffers, 8 V2 peers, 5-second frame/reply deadlines and 20-second idle expiry.
V1 has its own peer capacity; no payload is logged.

The descriptor identifies ubuntu-host/x86_64, Provider0.2.0, Ports ABI2 and
capabilities=0. SDL rendering belongs to the desktop, not this Provider.
localNetworkObservation=true only describes the V1 getifaddrs snapshot; it does
not claim Wi-Fi control, internet connectivity or a network V2 topic. This is
not a signed installation identity. Ping means this process responds, not that
the desktop, cloud binding or complete upgrade health check passed.
