# Platform providers

`a133` is the reference implementation. `a527`, `v883`, `rk3576` and `rv1106`
are intentionally fail-closed skeletons until the matching redistributable SDK
and a physical validation board are supplied. A skeleton is not a support
claim.

Each provider owns its SDK includes, device nodes, media APIs, kernel/DTS
overlay, toolchain, packaging and board validation profile. Shared code may
only consume interfaces from `core/ports` and capabilities from the manifest.
