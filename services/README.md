# Runtime service boundaries

The migration uses a hybrid process model. Resource ownership is strict even
while some legacy implementation remains hosted by `lv_backend`:

| Service | Exclusive resource or responsibility | Public control boundary |
| --- | --- | --- |
| `aitvbox-core` | product state and use-case orchestration | IPC v2 |
| `aitvbox-captured` | HDMI video/audio input | IPC v2 control + binary media |
| `aitvbox-inputd` | keyboard and relative-mouse gadgets | IPC v2 |
| `aitvbox-agentd` | observe/decide/act loop and safety policy | MCP + IPC v2 |
| `aitvbox-appd` | app install, permissions and sandbox | existing RPC, then IPC v2 |
| `aitvbox-updated` | verified A/B update and rollback | IPC v2 |

New callers must never open capture or HID device nodes directly. During the
migration, existing implementations are adapters behind these boundaries;
ownership is moved one service at a time and protected by contract tests.
