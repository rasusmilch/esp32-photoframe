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

`make test-semantic-controls` is a dependency-free strict C11 test of the pure semantic button
classifier. It covers configuration rejection, exact stable-time debounce and long-press boundaries,
noisy press/release samples, refresh versus exactly-once clear, previous/next without hold repeats,
held-at-wake suppression through release, independent simultaneous controls, unavailable controls,
and unsigned timestamp wrap. It also verifies that previous/next ignore long timing and that a
refresh/clear long threshold at or below debounce remains deterministic because long timing starts
only after press acceptance. Thresholds are injected test fixtures rather than product policy. The
classifier is not connected to GPIO, board mappings, wake handling, or production actions; firmware
integration and hardware validation remain pending.

`make test-build-helper` is a dependency-free standard-library Python test run first by `make test`.
It mocks ESP-IDF subprocesses and uses temporary directories to verify explicit `esp32s3` command
and environment selection, stale ambient/caller target handling, all supported board overlays,
debug and arbitrary `-D` forwarding, post-build arguments, and scoped `--fullclean` deletion. This
proves command construction only; it is not ESP-IDF compilation or hardware validation.

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
and proof that no poll reserves a second physical slot. The same target compiles the dependency-free
`connectivity_lifecycle.c` model under strict C11 warnings and proves exclusive slot ownership,
mode-mask stop evidence before fence, matching immutable fence identity, quarantine before release,
stale stop rejection, and fail-closed attributable post-fence activity. These are production-owner
ordering tests, not ESP-IDF event-loop or hardware validation.

`make test-wifi-epoch-trace` runs the standard-library schema-1 checker against synthetic pass/fail traces. Every physical `epoch_start` must have exactly one immutable active-state `attempt_outcome`—`success`, `failure`, `timeout`, or `replaced`—before stop and release. Driver outcomes require exact raw/symbolic evidence: `IP_EVENT`/0/`got_ip`, `WIFI_EVENT`/5/`sta_disconnected`, or `WIFI_EVENT`/12/`ap_start`; the standalone probe compile-time asserts those ESP-IDF values. Attempt success is not scenario completion.

Passing fixtures cover A–G plus the valid A and D first-attempt timeout alternatives. They require explicit second-attempt success in A/B/C/D/G, ordered generation replacement, D response and AP observation, E persistence/response/APSTA-to-STA/stable observation, mode-aware stop masks, exactly one fence, timed quarantine, release, and one `run_complete`; the probe owner also retains the per-attempt result sequence and gates completion on those same scenario outcomes. Negative fixtures and table-driven mutations cover schema/field/redaction failures, outcome identity/evidence/duplication, lifecycle and scenario ordering, fence/context/stop-mask faults, stale events, and premature completion. Focused mutations also prove that unexpected GOT_IP and disconnect results cannot be demoted, failed final retries cannot complete A/B/C/G, a generic disconnect cannot defer classification to timeout, stale terminal context including mode mismatch cannot classify a newer attempt, Scenario C requires update 2 then update 3 before replacement and stop, Scenario E observation failure cannot create a second outcome, stale APSTA→STA AP_STOP masks are rejected, stop requests must publish the stopping phase, and run_complete is terminal. Parser regressions accept only one complete JSON object followed by whitespace or recognized ANSI SGR terminal-control suffixes from monitor transport, while rejecting arbitrary trailing data, duplicate objects, malformed escapes, raw control data inside JSON strings, and in-object corruption. Temporary mutation traces are isolated and removed after each check. Every passing fixture is truncated before and after outcomes, stop requests, fences, quarantine completion, releases, and immediately before completion; all proper significant prefixes fail. Checker success validates only offline synthetic evidence, not ESP-IDF ordering or hardware. Representative E1002 physical probe evidence now covers B/C/D/E/G and opens the production connectivity lifecycle integration prerequisite for the epoch/fence model; A/F remain environment-limited physical debt, and full E1002/E1004 board validation remains pending.

## Required scenarios

- **Provisioning:** partial and one-byte reads; truncation; timeout; every field order; duplicate/missing/empty fields; malformed `%` escapes; encoded `&`, `=`, `%`, `+`, and spaces; decoded password lengths 63 valid/64 invalid; oversized body; no secret leakage.
- **Credential import:** valid/malformed file with existing credentials; NVS open/set/commit/readback failure; power loss before commit and after commit before deletion; unlink failure; retained committed file on reboot; both file locations; optional device name; no partial changes or secrets.
- **Boot/connectivity:** no credentials plus local images; valid credentials with AP unavailable for hours; default/configured retry and later success; no erasure, restart loop, or overlapping attempt; buttons during connection/provisioning; valid image not replaced.
- **Buttons:** bounce/debounce boundaries; short press; just below/at/above long threshold; held wake/release; exactly one long event and no following short event; simultaneous/unavailable controls; offline and provisioning states.
- **Navigation:** empty/one/many images; previous-first and next-last wrap; ordering independent of `readdir`; insertion/removal; album changes; display failure; deterministic cold-boot reconstruction without cursor persistence; concurrent timer/button/web actions.
- **Power/wake:** timer and each physical button; early wake; offline storage wake; URL network dependency; HA behavior; E1004 light-sleep restriction; per-board wake masks and storage preparation.

## State-lifetime implementation evidence

The follow-up implementation for REQ-STATE-* and DEC-018 remains pending. Its validation must keep host/source, firmware-build, and physical-hardware evidence distinct and must verify:

- repeated local rotation and timer-, button-, or web-originated navigation do not write `last_idx`, `last_image`, or an equivalent slideshow cursor/runtime position to NVS;
- successful ordinary SNTP and OTA periodic-task bookkeeping does not require NVS writes for `sntp_sync`, `ota_check`, or equivalent last-run timestamps;
- if RTC retention is implemented, deep-sleep continuity works without flash writes, retained data has validity/version protection, and absent, invalid, or incompatible retained data falls back safely;
- cold boot with no valid retained runtime state reconstructs deterministic, safe navigation and scheduling state, including tolerating an extra harmless SNTP or OTA check;
- durable operator configuration, Wi-Fi/static-network credentials, device/security settings, and other REQ-STATE-001 data still survive reset and power loss, so removal of transient writes does not weaken credential or configuration durability.

Source review or instrumented host fakes can establish which storage APIs are called. Firmware compilation establishes only build compatibility. Claims about RTC retention across deep sleep and durable configuration across real reset/power loss require applicable board/hardware procedures and observations in `docs/VALIDATION.md`; none are implied by this governance change.

For any replacement implementation, focused source review and search must identify directly superseded code, state, APIs, persistence artifacts, tests/fixtures, comments, and documentation. Evidence must show that obsolete competing mechanisms were removed or identify the current accepted requirement that retains each one; deletion itself needs no meaningless runtime test when inspection is sufficient. Keep this review scoped to artifacts directly superseded by the work.

## Build and hardware coverage

Build all IDs in `boards/boards.json`. E1002/E1004 matrices must cover the scenarios in `docs/VALIDATION.md`, including revision-specific power behavior. Other boards require GPIO/wake review and eventual physical checks, with unavailable controls explicit.

This adoption environment has no GitHub/network access, ESP-IDF, or supplied hardware. Firmware builds, dependency-fetching host tests, flashing, and physical validation are therefore environment-limited, not failed.
