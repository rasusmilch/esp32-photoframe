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

### Internal

- Added dependency-free normal-boot and generation-safe serialized retry policies, including a
  15-minute default, saturating deadlines, immutable attempt tokens, qualified connection events,
  and completion/cancellation ordering that prevents replacement overlap. Runtime connectivity
  behavior is unchanged pending integration.
- Added standalone provisional ESP-IDF Wi-Fi epoch-fence tooling with a standard-library schema-1
  checker and synthetic A–G validation. It preserves one normalized immutable outcome per physical
  attempt with exact GOT_IP, STA disconnect, AP_START, timeout, or replacement evidence; verifies
  synchronized attempt context, exact scenario ordering, mode-aware stop masks, Scenario E APSTA→STA transition evidence, stopping-phase
  publication before physical stop, one fence, terminal run completion, and a timed quiet quarantine;
  and rejects
  stale or demoted terminal evidence. Synthetic host validation passes, but production firmware is
  unchanged and physical E1002/E1004 acceptance remains pending.
