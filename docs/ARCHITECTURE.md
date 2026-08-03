# Architecture and boundaries

## Current implementation

`main/main.c` orchestrates boot and wake paths. `storage.c`, `album_manager.c`, and `display_manager.c` manage backends, albums, and display/rotation. `wifi_manager.c` and `wifi_provisioning.c` manage station connectivity and captive provisioning. `provisioning_form.c` is a pure C boundary that strictly parses bounded pointer-and-length form bodies and provides a callback-driven exact reader; the HTTP adapter validates the complete candidate and IPv4 values before configuration or Wi-Fi side effects. `wifi_import.c` is the pure positional `wifi.txt` parser, source-precedence selector, and transaction coordinator. `storage.c` owns bounded exact-path filesystem reads, while `wifi_import_runtime.c` adapts one NVS open/set/commit boundary, read-only verification, cache publication, and exact-path deletion. `power_manager.c` owns schedules, active/deep sleep, and wake dispatch. Board hardware is implemented under `components/board_hal`; Home Assistant, OTA, HTTP/mDNS, and periodic tasks are separate modules. Current cold boot still gates normal startup on provisioning/Wi-Fi and navigation lacks previous; these are known gaps, not target architecture.

`connectivity_policy.c` is a current pure decision boundary, but is not yet consumed by runtime. It
classifies normal storage/URL startup, credential state, local-service eligibility, asynchronous
connection/provisioning eligibility, retained-display preservation, and credential-store holds.
Its retry state separately records desired generation, immutable outstanding-attempt token,
connected generation, and retry generation/deadline. Credential replacement preserves an
outstanding physical slot; only matching completion or acknowledged cancellation releases it, and
only a later poll can reserve the newest generation. Connection/disconnection observations are
generation-qualified. Obsolete/unknown/duplicate events cannot alter desired connection or retry
state. Deadlines remain default/configurable and saturating.

## Required target boundaries

- **Boot coordinator:** decides cold-boot/wake behavior and initializes local-capable services before optional networking.
- **Normal initialization order:** board/storage, NVS and configuration cache, transactional
  `wifi.txt` import, Wi-Fi/provisioning initialization, then provisioning and connection decisions.
  Clear/timer/rotate fast-wake paths dispatch before the normal import. This ensures a verified
  imported device name is cached before the Wi-Fi manager derives its DHCP hostname.
- **Local slideshow domain:** owns deterministic inventory, current identity, refresh, previous/next, and persistence after display success.
- **Credential-import transaction:** current implementation discovers the config path before root, parses and stages without side effects, preserves the current device name when omitted, commits SSID/password/name together, reopens and verifies, publishes caches, then deletes the exact source. Complete, absent, incomplete (exactly one credential key), and operational-error profiles are distinct. A valid candidate repairs an incomplete pair through the same commit; equality with a complete verified profile provides idempotent deletion-only recovery after deletion failure.
- **Connectivity coordinator:** will adapt the existing pure boot/retry policy to non-blocking runtime state, serialized retries, and safe online/offline notifications without destructive side effects. Runtime integration remains pending.
- **Provisioning service:** receives a complete bounded body, strictly decodes and validates candidates, then activates atomically.
- **Semantic button service:** consumes HAL physical descriptors and implements debounce/duration state, emitting Wi-Fi-independent logical events.
- **Power/wake service:** owns active rotation, timer/button deep-sleep wake, early-wake correction, and board preparation.
- **Board HAL:** exposes physical capabilities—GPIO/polarity, display, storage, RTC, charger, sensors, USB detection, and sleep restrictions—not global product policy through misleading role names.
- **Optional integrations:** HTTP, mDNS, HA, URL rotation, OTA, and SNTP consume connectivity but never gate local slideshow or controls.

**Core dependency rule:** connectivity may enable network features, but must not own or gate local slideshow, storage navigation, buttons, or retained valid displayed content.

## Provisional physical-attempt fence

Production integration remains blocked pending physical validation of
`tools/wifi_epoch_fence_probe`. The proposed model assigns one immutable epoch/token/generation to
the single Wi-Fi owner. Cancellation/timeout marks it stopping and calls `esp_wifi_stop()`; the
slot remains held through application `WIFI_EVENT_STA_STOP`. That handler posts a custom fence to
the back of the same default ESP event loop, and only owner-task dispatch of that fence may release
the epoch. A later owner iteration may then start another epoch. Stop return, disconnect, delay,
yield, or cancellation request is not a fence. Any attributable old Wi-Fi/IP event after the fence invalidates the proposal. Validation evidence requires synchronized immutable snapshots for STA-stop and its copied fence payload, explicit fault records for evidence loss, and a complete stop/fence/release sequence followed by `run_complete`; truncated captures fail. Matching fence now begins a validation-only quiet quarantine that retains the old attributable context; any driver event fails the run, and release occurs only after a configured quiet interval and fresh fault checks. Scenario-specific milestones, rather than epoch counts, govern completion. Stop evidence is mode-aware (STA, AP, or both for APSTA), posts exactly one fence only after the applicable mask completes while stopping, and treats active AP-stop during APSTA-to-STA transition as ordinary evidence. Quarantine uses a recorded overflow-safe absolute monotonic deadline whose elapsed duration is checked offline. Schema-1 evidence additionally requires exactly one immutable, epoch-qualified terminal outcome per physical attempt, with exact raw GOT_IP, STA-disconnect, or AP-start evidence where applicable. Attempt success does not replace later scenario, fence, quarantine, release, and completion proof. This model is provisional, not a production guarantee.

## Open implementation design questions

Later design must settle image-identity representation and retry ownership. The import persistence boundary is resolved without credential-format migration: existing NVS keys share one explicit commit and readback, with equality-based recovery rather than a secret-derived marker. The provisioning body ceiling is 758 bytes. These technical choices cannot weaken `docs/GOVERNANCE.md`.
