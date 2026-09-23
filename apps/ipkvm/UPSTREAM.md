# IPKVM upstream

The web application and Go WebRTC backend under `upstream/` are derived from:

- Repository: `git@github.com:dshanpi/avaotaa1-kvmsource.git`
- Branch: `master`
- Commit: `d778c463`
- License: GPL-2.0, retained as `upstream/LICENSE`

The checked-in `upstream/static/` directory is the device UI build associated
with the same source line. It is embedded into the Go executable so firmware
builds do not need npm or network access.

The source repository omitted the Paraglide project inputs required by its UI
build. `upstream/ui/localization/{jetKVM.UI.inlang,messages}` restores those
inputs from the matching validated source tree; generated Paraglide output and
`node_modules` are intentionally not vendored.

AI-DeskTopBox additions are isolated in `platform_a133.go` and
`platform_hid_a133.go`, plus small platform-mode hooks in upstream files. The
original entry point remains available when `AITVBOX_PLATFORM_MODE` is unset.

The hardware H.264 sender in `video/` is ported from
`/home/ubuntu/kvm_video_demo`. It keeps the upstream socket contract:

- `/var/run/kvm_ctrl.sock`: JSON control actions
- `/tmp/kvm_video_stream.sock`: 4-byte little-endian length + Annex-B H.264

Both sockets are created mode `0600`. Their locations can be overridden for
host tests with `AITVBOX_KVM_CTRL_SOCKET` and `AITVBOX_KVM_VIDEO_SOCKET`.

Platform mode also uses one fixed WebRTC UDP port (`40000` by default). This
allows an FRP deployment to forward the HTTPS signaling endpoint and the media
transport explicitly instead of relying on a random UDP port range.
