# Architecture and boundaries

## Current implementation

`main/main.c` orchestrates boot and wake paths. `storage.c`, `album_manager.c`, and `display_manager.c` manage backends, albums, and display/rotation. `wifi_manager.c` and `wifi_provisioning.c` manage station connectivity and captive provisioning. `provisioning_form.c` is a pure C boundary that strictly parses bounded pointer-and-length form bodies and provides a callback-driven exact reader; the HTTP adapter validates the complete candidate and IPv4 values before configuration or Wi-Fi side effects. `wifi_import.c` is the pure positional `wifi.txt` parser, source-precedence selector, and transaction coordinator. `storage.c` owns bounded exact-path filesystem reads, while `wifi_import_runtime.c` adapts one NVS open/set/commit boundary, read-only verification, cache publication, and exact-path deletion. `power_manager.c` owns schedules, active/deep sleep, and wake dispatch. Board hardware is implemented under `components/board_hal`; Home Assistant, OTA, HTTP/mDNS, and periodic tasks are separate modules. Current cold boot still gates normal startup on provisioning/Wi-Fi and navigation lacks previous; these are known gaps, not target architecture.

## Required target boundaries

- **Boot coordinator:** decides cold-boot/wake behavior and initializes local-capable services before optional networking.
- **Normal initialization order:** board/storage, NVS and configuration cache, transactional
  `wifi.txt` import, Wi-Fi/provisioning initialization, then provisioning and connection decisions.
  Clear/timer/rotate fast-wake paths dispatch before the normal import. This ensures a verified
  imported device name is cached before the Wi-Fi manager derives its DHCP hostname.
- **Local slideshow domain:** owns deterministic inventory, current identity, refresh, previous/next, and persistence after display success.
- **Credential-import transaction:** current implementation discovers the config path before root, parses and stages without side effects, preserves the current device name when omitted, commits SSID/password/name together, reopens and verifies, publishes caches, then deletes the exact source. Complete, absent, incomplete (exactly one credential key), and operational-error profiles are distinct. A valid candidate repairs an incomplete pair through the same commit; equality with a complete verified profile provides idempotent deletion-only recovery after deletion failure.
- **Connectivity coordinator:** owns non-blocking state, serialized retries, and safe online/offline notifications without destructive side effects.
- **Provisioning service:** receives a complete bounded body, strictly decodes and validates candidates, then activates atomically.
- **Semantic button service:** consumes HAL physical descriptors and implements debounce/duration state, emitting Wi-Fi-independent logical events.
- **Power/wake service:** owns active rotation, timer/button deep-sleep wake, early-wake correction, and board preparation.
- **Board HAL:** exposes physical capabilities—GPIO/polarity, display, storage, RTC, charger, sensors, USB detection, and sleep restrictions—not global product policy through misleading role names.
- **Optional integrations:** HTTP, mDNS, HA, URL rotation, OTA, and SNTP consume connectivity but never gate local slideshow or controls.

**Core dependency rule:** connectivity may enable network features, but must not own or gate local slideshow, storage navigation, buttons, or retained valid displayed content.

## Open implementation design questions

Later design must settle image-identity representation and retry ownership. The import persistence boundary is resolved without credential-format migration: existing NVS keys share one explicit commit and readback, with equality-based recovery rather than a secret-derived marker. The provisioning body ceiling is 758 bytes. These technical choices cannot weaken `docs/GOVERNANCE.md`.
