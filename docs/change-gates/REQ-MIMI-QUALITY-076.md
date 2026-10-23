# REQ-MIMI-QUALITY-076 Full product quality review

User requested end-to-end review and optimization on 2026-10-23. Baseline public39eadb9/private dbcb5f1/SDK31a5653. Published rc31/rc8 remain immutable references. Existing incomplete acceptance is retained.

For every area: entry/navigation, normal operation, empty/unavailable, timeout/error, retry/cancel, leave/re-enter, restart/persistence, concurrency/ownership, security/data boundary. UI checks include real rendering, language/theme, clipping, touch targets, feedback, focus and destructive actions. Retain user-approved home and assistant design.

Workflow: inventory; relevant baselines; reproduce; prerecord minimal fixes; targeted regressions; actual rendered review; bounded prepared device validation; qualified new delivery. Host results never replace physical acceptance. Secrets stay private. No blind board stress, flash or power actions.

Prior HDMI slab fault, animation/touch, upgrade/rollback/power loss, data semantics and SDK source qualification remain open. Missing resources remain explicit without repeated credential requests.

| Area | Coverage | Source entry | Initial state |
|---|---|---|---|
| home | Home widgets navigation app lifecycle | apps/lv_port_linux/src/ui | not_run |
| settings | Persistence brightness volume language theme | apps/lv_port_linux/src/apps/setting | not_run |
| network | Wi-Fi Bluetooth reconnect timeout cancel | apps/lv_port_linux/src/apps | not_run |
| assistant | Conversation interruption pairing offline errors | apps/lv_port_linux/src/apps/ai | not_run |
| iot | IoT voice commands authorization result feedback | apps/lv_port_linux/src/apps/iot | not_run |
| hdmi | Signal audio display lifecycle ownership | apps/hdmi_preview | not_run |
| kvm | Authentication WebRTC freshness HID emergency stop | apps/ipkvm | not_run |
| apps | Signing install update uninstall permissions data | apps/platform_services | not_run |
| updates | Compatibility recovery rollback power interruption | tools/update | not_run |
| boot | Animation handoff readiness storage guards | packaging | not_run |
| data | Credentials settings KV backup recovery | integrations | not_run |
| delivery | Provenance edition boundaries signatures gates | scripts | not_run |

First batch: current source inventory, existing architecture/UI tests and isolated real LVGL rendering. Historical feature documentation must be reconciled with actual source; it is not acceptance evidence. Evidence: .delivery/quality-review-20261023.

First batch results: 98 architecture tests:96 passed,2 skipped because MIMI_A133_PROVIDER_ROOT unset.32 real LVGL renders and app-manager/page assertions passed, not physical validation. Visually reviewed8 Chinese/light pages; Bluetooth dark-theme contrast, assistant sidebar wrapping and update state coverage need follow-up. See first-batch-result.json. No product change yet.

Provider follow-up: both previously skipped checks passed against fixed root provider recipe ab6e835a; baseline coverage now98/98 across two runs. Bluetooth information container no longer dims while disconnected, connection hint foreground increased; individual unavailable transport/volume feedback retained.16 actual LVGL renders across disconnected/connected/details/rename and zh/en light/dark passed, Chinese disconnected full-size image visually inspected. Host-only, physical contrast/touch not_run. Next updater source review found nullable JSON text fields can reach strcmp when busy=true and phase=null; needs reproduction before fix. AI sidebar detailed review remains open.

Updater review: original exact helper extraction reproduced UBSAN null argument in strcmp for busy=true/phase=null; original failure retained. Text fields now accept strings only and return nonnull empty fallback; booleans require actual JSON booleans. Cancel is allowed only for known busy check/download states, matching current backend operations; install/unknown/null are disabled. Twelve malformed/valid cases compile exact helper bodies with -Wall -Wextra -Werror and UBSAN and pass. This does not qualify complete HTTP framing/deadlines, full status schema/action policy, worker lifetime or physical upgrade acceptance; follow-up remains. Evidence .delivery/quality-review-20261023/update-state.

Updater transport: exact original request function with only test socket-path substitution was held beyond4.5s by bytes arriving every200ms; harness killed its own client, failure retained. Replaced per-I/O timeout with nonblocking Unix socket and one monotonic3000ms deadline spanning connect/send/receive; EINTR/EAGAIN retry cannot reset deadline. Original real fragmented response, peer close, absent service, silent service and trickle tests now pass; existing12 field cases also pass. Action worker can make two sequential requests, hence up to roughly6s plus scheduling; do not claim3s entire action latency. No daemon, signature or device changes. Full HTTP framing/status schema/lifecycle still require further review.


QUALITY076 Assistant layout: original 800x480 screenshot clips panels; added real LVGL geometry assertions reproduced failure. Earlier40 render execution passes did not include these assertions and are not geometry acceptance. Content now follows actual display height, left panel grows into available width, labels follow panel width, compact spacing/orb preserve two-panel design, and right panel scrolls vertically. Paired-status copy shortened in both languages without removing unbind instructions.40 state/language/theme/size renders pass bounded-panel and final-note scroll reachability checks. Full1024 Chinese/light and800 Chinese/light plusEnglish/dark screenshots visually inspected. Physical touch/voice, pointer drag and other resolutions not_run. Evidence .delivery/quality-review-20261023/assistant-layout. Goal active.


QUALITY076 Assistant interaction: real LVGL input-driver pointer drag changes compact panel scroll; offline button tap sends no enter command; valid tap sends one enter and opens view, missing ack timeout closes with exit, retry works, pending close/reopen leaves no old timer firing.8 language/theme/size scenarios passed. Original English exact timeout-feedback assertion failed because fixed24px DOT text omitted trailing retry guidance; failure retained. Changed only feedback to content-height wrap with minimum24px;4 additional compact renders capture timeout state, English/light image visually verifies complete two-line guidance. Physical touch/voice not_run. Close currently may issue duplicate EXIT when both pending and overlay-open; retained for next idempotence/lifecycle review, not declared full lifecycle qualified. Evidence .delivery/quality-review-20261023/assistant-interaction.


QUALITY076 Assistant pending-close regression: exact command-count assertion reproduced duplicate EXIT when pending=true and overlay open. Shared stop_local_free_chat now snapshots whether exit is needed, cancels timer, clears pending, sends one EXIT and closes overlay; used by back navigation and app close, so subsequent app-manager close sees already-cleared local state.8 real LVGL pointer/timeout/retry/close cases pass including exactly-one EXIT assertion. No backend protocol or board changes. Timer allocation failure still sends ENTER after failure in existing handler; inspect with explicit allocation fault injection next. Evidence .delivery/quality-review-20261023/assistant-close.


QUALITY076 Assistant timer allocation failure: host link wrapper injects NULL once for armed6000ms timer; real handler originally still sent ENTER and left overlay open, assertion failure retained. Failure branch now closes local overlay, shows failure feedback and returns before ENTER.8 real LVGL language/theme/size interaction cases pass including injected failure, successful subsequent retry, normal timeout and one-exit pending close. Wrapper is host-only, no production fault hook. Not arbitrary out-of-memory resilience or physical backend acceptance. Next inspect late status/exit acknowledgement and backend ownership; full goal remains active. Evidence .delivery/quality-review-20261023/assistant-allocation.


QUALITY076 Assistant rejected-start lifecycle: real LVGL pointer start followed by inactive NETWORK_UNAVAILABLE or ERROR status reproduced orphan overlay after timer was canceled. Error branch now closes overlay and issues one EXIT; repeated identical status does not issue more exits and canceled timer remains quiet.16 state/language/theme/resolution cases including prior allocation/timeout/retry/close regressions pass. Active-mode status precedence unchanged; delayed/contradictory active acknowledgements and actual backend still require review. Canonical provider router located under hardware/overlay/openwrt/openwrt/package/allwinner/custom/smartdesktopmimi-a133-provider/{private,public}/code/apps/lv_port_linux/src/backend/backend_command_router.c; public application has interface headers, not selected backend producer. No board or publication changes. Evidence .delivery/quality-review-20261023/assistant-start-error.
