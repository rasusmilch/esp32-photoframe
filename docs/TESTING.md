# Testing strategy

Every normative requirement needs a host test, build/review check, or hardware-validation procedure. Hardware-only behavior must not be called host-tested, and compilation is not physical validation.

## Layers

1. Pure host units and component/state-machine simulations.
2. Storage/NVS fault injection and HTTP/form parsing.
3. Fake-clock connectivity scheduling and button edge/duration tests.
4. Deterministic navigation and concurrency tests.
5. All-supported-board firmware build matrix.
6. E1002/E1004 physical validation, then remaining-board preservation validation.
7. Security/log-redaction and documentation/governance checks.

Current host CMake tests cover cron and wake scheduling, while `make test` also runs CLI orientation tests. `make test-provisioning-form` is self-contained and compiles the pure provisioning module with C11, `-Wall`, `-Wextra`, `-Werror`, and `-pedantic`; the aggregate test target invokes it before dependency-fetching tests. It covers exact/partial reads, EOF/errors/bounded timeouts and sentinels, form ordering and unknown fields, strict escapes/forbidden bytes, duplicates, presence versus emptiness, every destination overflow, IP-mode requirements, the decoded 63/64-byte password boundary, and the derived 758-byte body boundary. Remaining adopted scenarios stay pending unless `docs/VALIDATION.md` records observation.

`make test-wifi-import` is also self-contained and runs before dependency-fetching tests. It covers LF/CRLF positional parsing, empty passwords and optional names, exact/overflow capacities and the 169-byte file ceiling, embedded/structural/excess input, both-path discovery and precedence, source failures, first/replacement/already-applied imports, optional-name preservation, simulated commit/readback failures and mismatches, exact-path deletion, deletion-failure recovery without a repeated commit, call ordering, and cache publication only after verification.

## Required scenarios

- **Provisioning:** partial and one-byte reads; truncation; timeout; every field order; duplicate/missing/empty fields; malformed `%` escapes; encoded `&`, `=`, `%`, `+`, and spaces; decoded password lengths 63 valid/64 invalid; oversized body; no secret leakage.
- **Credential import:** valid/malformed file with existing credentials; NVS open/set/commit/readback failure; power loss before commit and after commit before deletion; unlink failure; retained committed file on reboot; both file locations; optional device name; no partial changes or secrets.
- **Boot/connectivity:** no credentials plus local images; valid credentials with AP unavailable for hours; default/configured retry and later success; no erasure, restart loop, or overlapping attempt; buttons during connection/provisioning; valid image not replaced.
- **Buttons:** bounce/debounce boundaries; short press; just below/at/above long threshold; held wake/release; exactly one long event and no following short event; simultaneous/unavailable controls; offline and provisioning states.
- **Navigation:** empty/one/many images; previous-first and next-last wrap; ordering independent of `readdir`; insertion/removal; album changes; display failure; reboot persistence; concurrent timer/button/web actions.
- **Power/wake:** timer and each physical button; early wake; offline storage wake; URL network dependency; HA behavior; E1004 light-sleep restriction; per-board wake masks and storage preparation.

## Build and hardware coverage

Build all IDs in `boards/boards.json`. E1002/E1004 matrices must cover the scenarios in `docs/VALIDATION.md`, including revision-specific power behavior. Other boards require GPIO/wake review and eventual physical checks, with unavailable controls explicit.

This adoption environment has no GitHub/network access, ESP-IDF, or supplied hardware. Firmware builds, dependency-fetching host tests, flashing, and physical validation are therefore environment-limited, not failed.
