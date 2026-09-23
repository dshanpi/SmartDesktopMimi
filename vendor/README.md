# Vendor Binaries

This directory stores product-required binary dependencies that are not part
of the upstream open-source projects.

## TuyaOpen A133_B6 Libraries

`tuyaopen-a133-b6-libs/` contains the A133_B6 audio/KWS runtime libraries and
headers used by TuyaOpen:

- `audio_subsys/libaudio_subsys.a`
- `opus/libopus.a`
- `MNN/libMNN.so`
- `MNN/libMNN_Express.so`
- `alsa/` headers and fallback library

These files came from the working Tina SDK tree and are treated as vendor
binary inputs. They are required for reproducible A133 chatbot builds because
they are not present in the public TuyaOpen upstream checkout.

Use `tuyaopen-a133-b6-libs.sha256` to verify the exact files.
