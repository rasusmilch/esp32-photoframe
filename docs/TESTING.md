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

`make test-wifi-import` is also self-contained and runs before dependency-fetching tests. It covers LF/CRLF positional parsing, empty passwords and optional names, exact/overflow capacities and the 169-byte file ceiling, embedded/structural/excess input, both-path discovery and precedence, source failures, first/replacement/already-applied imports, optional-name preservation, both incomplete credential-pair forms and repair, pre-commit load failure versus post-commit verification failure, simulated commit/readback failures and mismatches, exact-path deletion, deletion-failure recovery without a repeated commit, call ordering, and cache publication only after verification.

`make test-connectivity-policy` is dependency-free and runs third, before downloaded test paths. Its
boot matrix covers storage versus URL mode, persistent-storage capability, all credential states,
retained-display preservation, asynchronous connection/provisioning eligibility, operational-error
hold, and explicit fast-wake exclusion. Its retry matrix covers the 15-minute default, arbitrary
nonzero intervals, initial/serialized attempts, failure deadlines, exact boundary polling, repeated
failure, success, unavailable credentials, and saturating `uint64_t` arithmetic. Race sequences
cover repeated replacement during an outstanding attempt, obsolete success/failure before and
after polling, immutable/unknown/duplicate tokens, cancellation request versus acknowledgement,
generation-qualified connection/disconnection, stale disconnects, retry-deadline preservation,
and proof that no poll reserves a second physical slot. The policy is not runtime validation.

`make test-wifi-epoch-trace` runs the standard-library schema-1 checker against synthetic pass/fail traces. Every physical `epoch_start` must have exactly one immutable active-state `attempt_outcome`—`success`, `failure`, `timeout`, or `replaced`—before stop and release. Driver outcomes require exact raw/symbolic evidence: `IP_EVENT`/0/`got_ip`, `WIFI_EVENT`/5/`sta_disconnected`, or `WIFI_EVENT`/12/`ap_start`; the standalone probe compile-time asserts those ESP-IDF values. Attempt success is not scenario completion.

Passing fixtures cover A–G plus the valid A and D first-attempt timeout alternatives. They require explicit second-attempt success in A/B/C/D/G, ordered generation replacement, D response and AP observation, E persistence/response/APSTA-to-STA/stable observation, mode-aware stop masks, exactly one fence, timed quarantine, release, and one `run_complete`; the probe owner also retains the per-attempt result sequence and gates completion on those same scenario outcomes. Negative fixtures and table-driven mutations cover schema/field/redaction failures, outcome identity/evidence/duplication, lifecycle and scenario ordering, fence/context/stop-mask faults, stale events, and premature completion. Focused mutations also prove that unexpected GOT_IP and disconnect results cannot be demoted, failed final retries cannot complete A/B/C/G, a generic disconnect cannot defer classification to timeout, stale terminal context including mode mismatch cannot classify a newer attempt, Scenario C requires update 2 then update 3 before replacement and stop, and Scenario E observation failure cannot create a second outcome. Temporary mutation traces are isolated and removed after each check. Every passing fixture is truncated before and after outcomes, stop requests, fences, quarantine completion, releases, and immediately before completion; all proper significant prefixes fail. Checker success validates only offline synthetic evidence, not ESP-IDF ordering, the proposed production fence, or hardware. The E1002 physical matrix remains pending, with E1004 afterward.

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
