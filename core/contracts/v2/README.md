# AITVBox IPC v2

Control messages use a four-byte unsigned big-endian length followed by one
UTF-8 JSON object. The maximum JSON body is 64 KiB. Requests and responses
carry a `requestId`; events carry a monotonic `sequence` for their producing
service. Unknown fields are retained for forward compatibility.

The canonical envelope is `envelope.schema.json`. `aitvbox_v2.py` is the host
reference codec used by contract tests and tooling. Video, audio and encoded
media never use this control channel.

The v2 backend endpoint is `/run/aitvbox/backend-v2.sock`. The legacy native-C
endpoint remains `/tmp/lv_port_linux_backend.sock` during the two-release
migration window.
