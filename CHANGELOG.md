# Changelog

This is the sole canonical changelog. Update it for user-visible behavior, operator workflow, security, hardware contracts, compatibility, persistence, migration, recovery, and significant documentation or governance changes.

## Unreleased

### Governance

- Added repository-wide authority routing and adopted offline-first and compatibility requirements.
- Added architecture, hardware, operations, testing, decision, and validation authorities.
- No firmware runtime behavior changed.

### Security

- Hardened captive provisioning with complete bounded body reception, strict URL-form decoding,
  decoded-length enforcement, duplicate-field rejection, and validation before configuration or
  Wi-Fi side effects.
- Replaced truncating `wifi.txt` parsing with a bounded positional parser and a verified single-
  commit NVS import transaction that preserves existing settings on pre-commit failure.

### Fixed

- Made `wifi.txt` consume-once: exact-source deletion occurs only after readback verification,
  retained committed files recover by deletion without rewriting credentials or restarting.
- Applied verified imported identity before Wi-Fi initialization in the same boot, and allowed a
  valid file to transactionally repair an incomplete stored SSID/password pair.
- Replaced blocking/destructive normal Wi-Fi startup with a serialized asynchronous production
  owner: local controls start without association, transient failure retains credentials and
  schedules the 15-minute-default retry, and sleep teardown holds ownership through stop evidence,
  default-loop fence, and quiet quarantine.
- Routed captive provisioning, candidate association, scans, APSTA→STA, power-save changes, and
  physical stop through the same owner. Successful candidates persist only after qualified GOT_IP,
  complete the HTTP response before AP teardown, and become authoritative without reboot.

### Internal

- Added dependency-free normal-boot and generation-safe serialized retry policies, including a
  15-minute default, saturating deadlines, immutable attempt tokens, qualified connection events,
  and completion/cancellation ordering that prevents replacement overlap. The production runtime
  now consumes this policy; production E1002/E1004 hardware validation remains pending.
- Added standalone provisional ESP-IDF Wi-Fi epoch-fence tooling with a standard-library schema-1
  checker and synthetic A–G validation. It preserves one normalized immutable outcome per physical
  attempt with exact GOT_IP, STA disconnect, AP_START, timeout, or replacement evidence; verifies
  synchronized attempt context, exact scenario ordering, mode-aware stop masks, Scenario E APSTA→STA transition evidence, stopping-phase
  publication before physical stop, one fence, terminal run completion, and a timed quiet quarantine;
  and rejects
  stale or demoted terminal evidence, while accepting only recognized ANSI SGR monitor suffixes
  after one complete trace JSON object. Representative E1002 physical validation now accepts
  B/C/D/E/G, retains failed C/E diagnostic runs, leaves A/F environment-limited, replaces
  arbitrary repeated identical executions with one accepted run plus diagnostic reruns, and opens
  the production connectivity integration gate while broader E1002/E1004 hardware validation remains
  pending.
