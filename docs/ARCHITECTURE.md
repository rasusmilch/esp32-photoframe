# Architecture and boundaries

## Current implementation

`main/main.c` orchestrates boot and wake paths. `storage.c`, `album_manager.c`, and `display_manager.c` manage backends, albums, and display/rotation. `wifi_manager.c` and `wifi_provisioning.c` manage station connectivity and captive provisioning. `provisioning_form.c` is now a pure C boundary that strictly parses bounded pointer-and-length form bodies and provides a callback-driven exact reader; the HTTP adapter validates the complete candidate and IPv4 values before configuration or Wi-Fi side effects. `power_manager.c` owns schedules, active/deep sleep, and wake dispatch. Board hardware is implemented under `components/board_hal`; Home Assistant, OTA, HTTP/mDNS, and periodic tasks are separate modules. Current cold boot still gates normal startup on provisioning/Wi-Fi and navigation lacks previous; these are known gaps, not target architecture.

## Required target boundaries

- **Boot coordinator:** decides cold-boot/wake behavior and initializes local-capable services before optional networking.
- **Local slideshow domain:** owns deterministic inventory, current identity, refresh, previous/next, and persistence after display success.
- **Credential-import transaction:** parse, validate, stage, durably save, commit/verify, consume the exact file, and recover intermediate failures.
- **Connectivity coordinator:** owns non-blocking state, serialized retries, and safe online/offline notifications without destructive side effects.
- **Provisioning service:** receives a complete bounded body, strictly decodes and validates candidates, then activates atomically.
- **Semantic button service:** consumes HAL physical descriptors and implements debounce/duration state, emitting Wi-Fi-independent logical events.
- **Power/wake service:** owns active rotation, timer/button deep-sleep wake, early-wake correction, and board preparation.
- **Board HAL:** exposes physical capabilities—GPIO/polarity, display, storage, RTC, charger, sensors, USB detection, and sleep restrictions—not global product policy through misleading role names.
- **Optional integrations:** HTTP, mDNS, HA, URL rotation, OTA, and SNTP consume connectivity but never gate local slideshow or controls.

**Core dependency rule:** connectivity may enable network features, but must not own or gate local slideshow, storage navigation, buttons, or retained valid displayed content.

## Open implementation design questions

Later design must settle image-identity representation, retry ownership, and the precise NVS transaction mechanism. The provisioning body ceiling is resolved at 758 bytes: three times the sum of current decoded value maxima, plus known field names, equals signs, and separators. These technical choices cannot weaken `docs/GOVERNANCE.md`.
