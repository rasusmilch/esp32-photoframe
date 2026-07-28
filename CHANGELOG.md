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
