# Tuya Smart Home bridge

This service runs on a trusted server, separately from the desktop. It is not
installed by the desktop application package. The Tuya Access ID/Secret stay on
that server; a desktop receives a random token restricted to one authorized
Tuya UID and explicitly allowed device categories. No project key is compiled
into the public desktop. Existing Tuya Assistant binding is a different service
and does not automatically authorize Cloud API device control.

Build: `go test -race ./...`, `go vet ./...`, `go build -o mimi-iot-bridge .`.
Start with `--config /protected/iot-server.json --tls-cert /protected/server.crt
--tls-key /protected/server.key`. Only HTTPS is served. Default listener is
127.0.0.1:9845; set `--listen` explicitly for a reachable TLS endpoint.

The server configuration is a regular file, owned by the service user, mode
0600. Required fields are `endpoint` (official regional Tuya API origin),
`access_id`, `access_secret`, and `token_sha256_grants`. The latter maps each
lowercase SHA256 digest of a separately generated desktop token to an object
with `uid` and `control_categories`. Empty categories grant read-only access.
Generate at least 32 random bytes per desktop and encode as hexadecimal or
base64url. Transfer the token through the administrator's protected channel;
never put credentials into Git, screenshots, logs, manifests, or chat.

The device's `/etc/smartdesktopmimi/iot-client.json` contains exactly `endpoint`
(the bridge HTTPS origin including port, without a path) and `token`.
The file must be mode 0600, owned by the desktop process user, and not a symlink.
The server certificate must chain to the device trust store; TLS verification
is mandatory. Revocation means removing that token's server grant. This path
is external user configuration and must be retained during application updates.

Routes:

- GET /v1/devices: account-scoped devices and observed state, with cloud secrets
  and local device keys excluded.
- GET /v1/devices/ID/functions: advertised data points and whether control is
  allowed by the account grant and online status.
- POST /v1/devices/ID/commands: one `{code,value}` command. Membership, online
  state, category, advertised type, bounds and step are checked again.

The native desktop supports Boolean, Integer and Enum controls. Every command
requires confirmation. Accepted commands are not shown as completed device
state; the user refreshes observed state. Failed or timed-out commands are not
retried automatically. Unsupported Raw/String operations are read-only.
Device discovery/binding remains in Smart Life and the cloud project account
linking process; this bridge does not claim to provision arbitrary radios.

Official references consulted on 2026-10-13:
- https://developer.tuya.com/cn/docs/iot/new-singnature?id=Kbw0q34cs2e5g
- https://developer.tuya.com/en/docs/cloud/device-management?id=K9g6rfntdz78a
- https://developer.tuya.com/en/docs/cloud/device-control?id=K95zu01ksols7
- https://developer.tuya.com/en/docs/developer/apply-cloud-api-key?id=Kff30z8sv62ah

Status: host protocol, permission and LVGL fixture tests passed. Live Tuya
account/device acceptance is NOT RUN pending authorized cloud inputs and a
real test device. A133 UI compilation is not evidence of real device control.
