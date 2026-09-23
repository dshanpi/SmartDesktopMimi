# Security policy

Do not open a public issue containing credentials, device identities, private
keys, unpublished firmware, customer data or exploitable deployment details.
Report security issues privately through the security contact published on the
official 09make.inc project page.

## Supported releases

Only the newest tagged public release and the current development branch
receive security fixes. Hardware marked `skeleton` is not supported.

## Non-negotiable controls

- Cloud and OTA HTTPS verify both the certificate chain and hostname.
- OTA images require hash and signature verification plus health rollback.
- Local IPC authenticates peers and uses least-privilege socket modes.
- USB HID always releases keys and buttons on exit, timeout and emergency stop.
- Factory data is provisioned from protected runtime storage and never logged.
- Public releases are created from a clean audited snapshot, not from the
  current development repository's historical object database.

Before a public release, rotate every production secret that has ever appeared
in this repository, run a full-tree and history secret scan, review binary
redistribution rights, and build from a fresh clone.
