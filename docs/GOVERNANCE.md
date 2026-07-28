# Governance, requirements, and security

This document is normative. “Must” requirements are accepted; current firmware gaps are implementation work, not reasons to weaken them.

## Requirements

### Platform

- **REQ-PLATFORM-001:** Preserve every board in `boards/boards.json` and the build matrix.
- **REQ-PLATFORM-002:** reTerminal E1002 and E1004 are the highest-priority implementation and physical-validation targets.
- **REQ-PLATFORM-003:** Do not silently remove board-specific features; insufficient buttons may make a dedicated action explicitly unavailable.
- **REQ-PLATFORM-004:** Source-derived hardware claims remain pending until physically validated.

### Offline operation

- **REQ-OFFLINE-001:** Local-storage cold boot, display, scheduled rotation, and manual navigation must work without Wi-Fi.
- **REQ-OFFLINE-002:** Valid displayed content must remain during network outage.
- **REQ-OFFLINE-003:** Provisioning must not replace valid displayed content merely because Wi-Fi failed.

### Wi-Fi

- **REQ-WIFI-001:** Wi-Fi is asynchronous and additive in local mode; it must not own storage, slideshow, buttons, or displayed-image lifecycle.
- **REQ-WIFI-002:** Background retry defaults to 15 minutes and is configurable.
- **REQ-WIFI-003:** Retry attempts must be serialized and non-overlapping.
- **REQ-WIFI-004:** Transient failure must not erase last-known-good credentials or trigger a reboot loop.
- **REQ-WIFI-005:** Disconnect reason codes must be logged usefully without secrets.

### Credentials and provisioning

- **REQ-CREDENTIAL-001:** Completely parse, validate, and stage candidate input before persistence; preserve valid credentials on every earlier failure.
- **REQ-CREDENTIAL-002:** A valid `wifi.txt` is consumed once: durably save and commit/verify all related settings before deleting the exact source file.
- **REQ-CREDENTIAL-003:** Deletion failure after commit must not cause repeated import or restart; device name/network settings must not be partially persisted.
- **REQ-PROVISION-001:** Receive a complete, bounded HTTP body and parse fields independently of order.
- **REQ-PROVISION-002:** Strictly validate percent escapes and decoded, not encoded, lengths; accept valid WPA passphrases through 63 decoded bytes.
- **REQ-PROVISION-003:** Partial or invalid configuration must not activate, and secret input must not be logged.

### Controls and navigation

- **REQ-CONTROL-001:** Buttons and recovery controls must be available before network success and during attempts, provisioning, and failure.
- **REQ-CONTROL-002:** Logical actions must be separate from physical GPIO names and work offline without Home Assistant.
- **REQ-CONTROL-003:** Debouncing and duration handling must make short/long actions unambiguous; bounce, short presses, and held wake transitions must not clear.
- **REQ-NAV-001:** Provide refresh-current, previous, and next over deterministic image ordering with explicit wraparound.
- **REQ-NAV-002:** Persist navigation only after display success; handle insertions/removals predictably and serialize timer, button, and web actions.

### Images, security, and compatibility

- **REQ-IMAGE-001:** Local discovery supports BMP, PNG, and EPDGZ. Direct SD JPEG discovery is deferred; JPEG continues through web upload/conversion.
- **REQ-SEC-001:** Never log passwords, tokens, authorization/secret headers, full forms, or credential-file contents.
- **REQ-SEC-002:** Treat SD and captive-portal input as untrusted; use bounded parsing and preserve last-known-good data.
- **REQ-SEC-003:** Distinguish safe recovery/replacement from destructive reset and preserve OTA/certificate trust boundaries.
- **REQ-COMPAT-001:** Preserve URL mode, Home Assistant, OTA, mDNS, web UI, APIs, upload/conversion, sleep/wake, storage backends, and every supported board.
- **REQ-COMPAT-002:** Local-mode changes must not turn URL mode into offline mode; board exceptions remain explicit.

## Security model

Assets include Wi-Fi credentials, HA/API tokens, authorization and custom headers, certificates, static-network configuration, and persistent device settings. Trust boundaries are removable SD media, captive-portal clients/AP exposure, the LAN, remote URL/OTA services, and NVS. Parse and bound untrusted input, activate candidates atomically, redact secrets, retain last-known-good state on transient failure, and reserve erasure for explicit operator reset/replacement. Existing certificate pinning and OTA verification boundaries must not be weakened.

## Roadmap

1. Governance bootstrap. 2. Provisioning parser hardening. 3. Transactional `wifi.txt`. 4. Host-testable boot/connectivity policy. 5. Offline-first lifecycle/retry. 6. Disconnect diagnostics. 7. Semantic buttons. 8. Deterministic navigation. 9. Cross-board power/wake preservation. 10. Expanded host tests/CI. 11. E1002/E1004 validation. 12. Remaining-board validation. 13. Documentation/release closure.

## Deferred work

Direct SD JPEG discovery, unrelated UI enhancements, extra gestures beyond accepted mappings, unvalidated speculative hardware behavior, and nonessential governance templates are deferred. This does not reopen accepted requirements.
