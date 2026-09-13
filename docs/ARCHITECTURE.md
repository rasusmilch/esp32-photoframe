# Architecture and boundaries

## Current implementation

`main/main.c` orchestrates boot and wake paths. `storage.c`, `album_manager.c`, and `display_manager.c` manage backends, albums, and display/rotation. `connectivity_runtime.c` is the single serialized owner of lifecycle-affecting ESP-IDF Wi-Fi operations; `wifi_manager.c` is its compatibility/configuration adapter and `wifi_provisioning.c` submits AP, candidate, scan, and APSTA→STA work to that owner. `provisioning_form.c` is a pure C boundary that strictly parses bounded pointer-and-length form bodies and provides a callback-driven exact reader; the HTTP adapter validates the complete candidate and IPv4 values before configuration or Wi-Fi side effects. `wifi_import.c` is the pure positional `wifi.txt` parser, source-precedence selector, and transaction coordinator. `storage.c` owns bounded exact-path filesystem reads, while `wifi_import_runtime.c` adapts one NVS open/set/commit boundary, read-only verification, cache publication, and exact-path deletion. `power_manager.c` owns schedules, active/deep sleep, and wake dispatch. Board hardware is implemented under `components/board_hal`; Home Assistant, OTA, HTTP/mDNS, and periodic tasks are separate modules. Normal cold boot now starts local controls/services without waiting for association; navigation still lacks previous.

`connectivity_policy.c` is the pure decision boundary consumed by the production runtime. It
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
- **Local slideshow domain:** owns canonical inventory and identity, the identity-based cursor, refresh, previous/next, and logical-position advancement after display success. Its active cursor is runtime state with selected internal RTC deep-sleep continuity, not a per-navigation durable-write boundary.
- **Credential-import transaction:** current implementation discovers the config path before root, parses and stages without side effects, preserves the current device name when omitted, commits SSID/password/name together, reopens and verifies, publishes caches, then deletes the exact source. Complete, absent, incomplete (exactly one credential key), and operational-error profiles are distinct. A valid candidate repairs an incomplete pair through the same commit; equality with a complete verified profile provides idempotent deletion-only recovery after deletion failure.
- **Connectivity coordinator:** `connectivity_runtime.c` owns one long-lived command/event queue, immutable epoch/attempt/generation identity, retry-policy state, connection deadlines, mode-aware stop evidence, default-loop fence dispatch, quiet quarantine, and fail-closed reuse. `connectivity_lifecycle.c` is its dependency-free ordering model for host regression tests. Compatibility APIs may wait for a qualified result, but cannot call lifecycle-affecting driver APIs themselves.
- **Provisioning service:** receives a complete bounded body, strictly decodes and validates candidates, then activates atomically.
- **Semantic button service:** consumes HAL physical descriptors and implements debounce/duration state, emitting Wi-Fi-independent logical events.
- **Power/wake service:** owns active rotation, timer/button deep-sleep wake, early-wake correction, and board preparation.
- **Board HAL:** exposes physical capabilities—GPIO/polarity, display, storage, RTC, charger, sensors, USB detection, and sleep restrictions—not global product policy through misleading role names.
- **Optional integrations:** HTTP, mDNS, HA, URL rotation, OTA, and SNTP consume connectivity but never gate local slideshow or controls.

**Core dependency rule:** connectivity may enable network features, but must not own or gate local slideshow, storage navigation, buttons, or retained valid displayed content.

## State-lifetime boundary

State is classified by the lifetime that product correctness actually requires:

1. **Durable configuration/state** uses NVS or another explicitly durable store because operator configuration, credentials/security state, or another accepted correctness requirement must survive reset and power loss. This includes the durable configuration named by REQ-STATE-001; the new boundary does not weaken credential, provisioning, or integration durability.
2. **Runtime state** uses RAM and is disposable or reconstructable. Ordinary navigation position and scheduler bookkeeping are in this class, and cold boot must rebuild safe values rather than depend on flash bookkeeping.
3. **Deep-sleep continuity state** may use RTC-retained memory when continuity materially improves behavior, but may be lost on reset or power removal. Any retained representation must include validity and version checks with safe fallback. It should not exist when reconstruction is simpler and harmless.

The current implementation has removed the NVS-backed `last_image` mechanism: storage-rotation repeat avoidance now keeps only the most recently successful rotation image in volatile RAM and may forget it across deep sleep or reset. Remaining implementation debt includes `last_idx` in the `photoframe` NVS namespace for sequential progression, `last_fetch_err` for a changing fetch diagnostic across sleep/boot, and `sntp_sync` and `ota_check` in the `periodic` NVS namespace for periodic last-run timestamps. Follow-up implementation must remove or redesign those remaining NVS-backed runtime paths. DEC-020 now resolves the identity and deep-sleep cursor target needed to replace `last_idx`, but that replacement is not implemented. Future local slideshow architecture must not depend on an NVS write for each rotation or navigation event. SNTP and OTA scheduling must not depend on durable last-run timestamps: optional RTC retention may bridge deep sleep, or a cold boot may restart the schedule and perform an extra harmless check, unless later accepted authority establishes a power-loss durability need.

## Canonical local slideshow identity and cursor

DEC-020 closes the local image-identity question for internal slideshow/navigation use. A canonical identity is the exact path relative to `IMAGE_DIRECTORY`, with no mount prefix or leading slash: `album-name/filename.ext` (for example, `Default/sunrise.png`). It includes both album and filename, preserves their exact byte spelling and case, and compares case-sensitively. A basename is insufficient across albums; an absolute `/storage/images/...` path is an execution path, not identity. This internal representation is not a new public API compatibility promise.

The canonical inventory contains slideshow-eligible regular BMP, PNG, and EPDGZ files from currently enabled albums; direct JPEG discovery remains deferred. Exact duplicate identities are one logical item. Inventory construction must sort ascending by bytewise C-string `strcmp` over canonical identities and must not depend on filesystem `readdir()` order or enabled-album order.

The cursor is the canonical identity of the last successfully committed local slideshow position. Display failure does not advance it. Given the sorted inventory:

- empty inventory has no NEXT or PREVIOUS target;
- with no cursor, NEXT selects the first identity and PREVIOUS the last;
- with an exactly present cursor, NEXT selects the following identity and PREVIOUS the preceding identity, wrapping at each end;
- with a missing cursor, NEXT selects the first identity strictly greater than the old cursor, wrapping to first if none exists, while PREVIOUS selects the greatest identity strictly less than the old cursor, wrapping to last if none exists; and
- a one-item inventory wraps to that same item.

A structurally valid identity retained after removal, rename, or album disable remains the insertion point for these missing-cursor rules. It is not replaced by a numeric position.

Normal scheduled and manual operation spans deep-sleep restarts, so the target uses internal ESP32 RTC-retained memory for cursor continuity. Retained cursor data represents the canonical identity, carries an explicit schema version and bounded structural validity, and falls back safely when absent, malformed, incompatible, or invalid. It may be consumed only following a genuine deep-sleep wake. Power loss, power-on, software, panic/watchdog, brownout, and every other non-deep-sleep reset treat the cursor as absent even if retained bytes remain. This is internal SoC memory, not an external PCF8563 or other board RTC, and it performs no NVS/flash write.

Display publication remains a separate concern: `current_image` and `.current.lnk` report displayed content and are not cursor authority or a cursor reconstruction source. RAM-only `last_displayed_image` remains separate best-effort random-repeat state and may be forgotten across sleep/reset.

This is target architecture, not current implementation. The numeric, NVS-backed `last_idx` still drives current sequential traversal and remains debt until replaced; no migration or RTC cursor exists yet.

## Production physical-attempt fence

Representative E1002 physical validation of `tools/wifi_epoch_fence_probe` has lifted the standalone epoch-fence prerequisite for beginning production connectivity lifecycle integration. The accepted model assigns one immutable epoch/token/generation to
the single Wi-Fi owner. Cancellation/timeout marks it stopping and calls `esp_wifi_stop()`; the
slot remains held through application `WIFI_EVENT_STA_STOP`. That handler posts a custom fence to
the back of the same default ESP event loop, and only owner-task dispatch of that fence may release
the epoch. A later owner iteration may then start another epoch. Stop return, disconnect, delay,
yield, or cancellation request is not a fence. Any attributable old Wi-Fi/IP event after the fence invalidates the proposal. Validation evidence requires synchronized immutable snapshots for STA-stop and its copied fence payload, explicit fault records for evidence loss, and a complete stop/fence/release sequence followed by `run_complete`; truncated captures fail. Matching fence now begins a validation-only quiet quarantine that retains the old attributable context; any driver event fails the run, and release occurs only after a configured quiet interval and fresh fault checks. Scenario-specific milestones, rather than epoch counts, govern completion. Stop evidence is mode-aware (STA, AP, or both for APSTA), posts exactly one fence only after the applicable mask completes while stopping, and treats active AP-stop during APSTA-to-STA transition as ordinary evidence. Quarantine uses a recorded overflow-safe absolute monotonic deadline whose elapsed duration is checked offline. Schema-1 evidence additionally requires exactly one immutable, epoch-qualified terminal outcome per physical attempt, with exact raw GOT_IP, STA-disconnect, or AP-start evidence where applicable. Attempt success does not replace later scenario, fence, quarantine, release, and completion proof. B/C/D/E/G have accepted representative E1002 physical evidence, while A/F remain environment-limited debt; this gate decision is not full E1002/E1004 board validation or a production release guarantee.

The production owner now implements that lifecycle boundary without importing the probe trace schema. Driver callbacks enqueue captured physical identity; the owner alone classifies results and issues lifecycle calls. Stop publishes stopping state before `esp_wifi_stop()`, accumulates the requested mode's stop mask, posts one copied-context event on the default loop, quarantines for two seconds, and fails closed on mismatched or post-fence evidence. This implementation has host/source validation only; the standalone probe's physical evidence does not make the production runtime hardware-validated.

## Open implementation design questions

Retry ownership is resolved by DEC-017, and local slideshow identity/cursor semantics are resolved by DEC-020. The import persistence boundary is resolved without credential-format migration: existing NVS keys share one explicit commit and readback, with equality-based recovery rather than a secret-derived marker. The provisioning body ceiling is 758 bytes. These technical choices cannot weaken `docs/GOVERNANCE.md`.
