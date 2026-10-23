# HDMI audio follow-up - 2026-10-23

REQ-MIMI-HDMI-074; controlled delivery REQ-MIMI-DELIVERY-058.

## Observed outcome

The user reported HDMI silence with an AMD source computer, despite normal Tuya conversation speaker output. Later the user connected another computer, selected BWW6911 (NVIDIA High Definition Audio), and explicitly confirmed audible sound. This is **passed_user_confirmed** for that source/device combination only; no objective waveform or duration acceptance was performed at the moment of confirmation.

During this audio investigation no A133 firmware, driver, mixer setting or audio configuration was changed by the agent. Earlier observations included no incoming frames, then advancing capture counters with zero-valued PCM. Source inspection verifies the amplitude statistics are taken immediately from captured samples, before resampling and playback. Zero samples do not establish whether silence originated at the source, HDMI receiver or A133 input, and must not be described as proof of an A133 regression.

The AMD screenshots show a BWW endpoint and later a Microsoft generic High Definition Audio driver. This alone does not establish a driver replacement or causal defect. Changing computers also reconnects HDMI and renegotiates capabilities. Driver/output selection/handshake compatibility remains **review_required**, not a proven AMD driver root cause. The earlier categorical implication of an A133 input fault is withdrawn; original evidence remains retained.

## Artifact and evidence binding

System v1.0.0-rc.8, factory application v1.0.8-rc.31; public build source 019a27f050d05e96e37f5b55e1bb6a82c155ef17; SDK 31a5653bea157bcc61e286ceca3d2a20a48e4507. System archive SHA256 abd4e6d12cd050d3aa6231a1d251289fe4239a3c7ea1ab465c10a28597caf88b.

Retained rc7-r4/rc8-r2 images contain byte-identical hdmi_preview and asound.conf. This excludes changes to those two artifacts between the images, not all receiver/kernel behavior. Scoped raw diagnostics remain private in hdmi-audio-incident-20261023-r1; no raw device logs, identities, credentials or desktop screenshots are published. User confirmation is the evidence for actual audible output, not earlier silent samples.

## Remaining gates

No new product build or image is required for this documentation-only result. Existing signed artifacts and version tags stay unchanged. HDMI long-duration stability and the original slab fault, physical animation/touch, upgrade/rollback/power interruption, complete data semantics and source qualification remain open. No stable-release or general source-computer compatibility claim. A future controlled AMD retest should distinguish output selection, actual EDID/handshake and driver identity without assuming the cause.
