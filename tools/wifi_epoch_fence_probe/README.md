# Wi-Fi epoch-fence probe

This standalone ESP-IDF application tests a **provisional** physical-attempt barrier. It is not linked into production firmware and cannot establish safety until the physical matrix passes. Traces never contain credentials.

## Ownership, synchronization, and fence

One owner task exclusively writes the active context and calls Wi-Fi mode, configuration, start, connect, and stop APIs. Wi-Fi, IP, and fence callbacks take an internally consistent snapshot under the same short `portMUX_TYPE` critical section used by owner writes; no lock covers a driver call, event post, queue send, or trace print, and 64-bit access is never assumed atomic.

On stop, the owner marks the immutable epoch/attempt/generation/owner/requested-mode identity stopping and calls `esp_wifi_stop()` once. Separate synchronized evidence requires mask 1 (STA stop) for STA, mask 2 (AP stop) for AP, and mask 3 (both) for APSTA. Inapplicable or duplicate stop events fail. A stop event while active is only a driver observation and cannot fence. Exactly one copied-context fence is posted only when the full required mask is observed; callbacks never release. The owner verifies identity and mask completion before accepting it. A stop return, disconnect, delay, yield, or cancellation request is not a fence.

Queue overflow, fence-post failure, snapshot mismatch, driver failure, repeated stop, start overlap, and fence timeout latch a callback-safe probe fault. The owner emits the fault when it can run, does not release unsafe state, and cannot emit successful completion. Initialization failures before tracing is available remain an explicit limitation.

## Trace and completion

Every `EPOCH_TRACE ` JSON object uses the exact `trace_schema_version=1` key set. The probe compile-time asserts the schema constants `IP_EVENT_STA_GOT_IP=0`, `WIFI_EVENT_STA_DISCONNECTED=5`, and `WIFI_EVENT_AP_START=12`. Raw base, numeric ID, and symbolic event must agree. Unknown or additional fields and secret-bearing fields are rejected.

Every `epoch_start` has exactly one owner-task-classified `action="attempt_outcome"` while active and before stop. Its result is exactly `success`, `failure`, `timeout`, or `replaced`, and its run, scenario, epoch, attempt ID, generation, owner, requested mode, quarantine interval, and schema version match the immutable start identity. Driver evidence is the outcome row itself: STA success is `IP_EVENT`/0/`got_ip`, STA failure is `WIFI_EVENT`/5/`sta_disconnected` with its reason, and AP-only success is `WIFI_EVENT`/12/`ap_start`. Owner timeout is `PROBE`/0/`attempt_timeout`; replacement is `PROBE`/0/`replacement_requested`. Callbacks only enqueue synchronized evidence; outcome bookkeeping remains owner-task-local.

Attempt success means only that its physical success event occurred. Persistence, response, mode transition, stable observation, stop-mask completion, fence, quarantine, release, fault checks, and exactly one final `run_complete` remain independent requirements for scenario success. Probe/infrastructure faults never fabricate outcomes or completion.

A matching fence records its monotonic timestamp and overflow-checked absolute deadline, then begins validation-only quiet quarantine without releasing context. Queue activity cannot reset the deadline. Any Wi-Fi/IP event before release fails. Completion is accepted only at or after `CONFIG_PROBE_POST_FENCE_OBSERVE_MS` (default 2000 ms), after which the owner rechecks faults, releases, and may schedule a later epoch. This evidence delay is not part of the proposed production barrier.

The scenario matrix is: A uses first-attempt STA failure or timeout, then exact GOT_IP success; B replaces generation 1 after update 2, then generation 2 succeeds; C replaces generation 1 after ordered updates 2 and 3, never starts generation 2, then generation 3 succeeds; D uses first APSTA failure or timeout followed by response completion, then exact AP_START success and AP observation completion; E uses exact GOT_IP success followed by persistence, response, successful APSTA-to-STA transition, and quiet stable-STA observation; F has one timeout and no GOT_IP; G orders API submission, update 2, replaced outcome, and stop before generation 2 exact GOT_IP success. D/E quiet-observation expiry records observation completion, not another outcome. Scenario E active transition AP_STOP remains ordinary evidence and cannot fence; its later physical stop requires STA_STOP.

## Offline checker

Run `make test-wifi-epoch-trace`, or:

    python3 tools/wifi_epoch_fence_probe/check_trace.py capture.log

The checker enforces stable scenario identity, scenario-specific milestones and phase completion, increasing sequence/nondecreasing time, unique IDs, one owner, per-epoch AP/STA configuration, exact stop/fence/quarantine/release ordering and context, symbolic event semantics, no post-fence driver event, no hidden reconnect, no evidence-loss fault, and terminal completion. ESP-IDF monitor text may precede the `EPOCH_TRACE ` prefix, and recognized ANSI SGR terminal-control suffixes after one complete JSON object are treated as transport decoration; arbitrary trailing data, duplicate JSON objects, duplicate trace objects on one line, malformed escapes, and corruption inside JSON remain invalid evidence. Raw monitor captures are authoritative and must not be manually cleaned before validation. Synthetic tests require every named negative fixture to fail for its intended reason and deliberately truncate every passing fixture.

## ESP-IDF execution

Use the repository-supported ESP-IDF v6.0 family or the exact version later pinned for firmware.
Record `idf.py --version` and the ESP-IDF git commit. Starting with E1002, then E1004:

    cd tools/wifi_epoch_fence_probe
    idf.py set-target esp32s3
    idf.py menuconfig
    idf.py -p PORT flash monitor | tee scenario-N-run-M.log
    python3 check_trace.py scenario-N-run-M.log

Select the scenario, test-only AP SSID, and password in “Wi-Fi epoch fence probe”. Never publish
an unredacted general monitor log. For every artifact record board/revision, power source, AP model
and firmware, signal conditions, scenario, IDF version/commit, probe commit, and result.

## Physical validation policy

One accepted physical execution establishes that scenario's required real-driver path for this standalone probe. Repeat a scenario after a failed run, anomaly, timing-sensitive finding, relevant implementation change, or intentionally different environment; failed runs remain retained evidence. Synthetic host tests remain responsible for exhaustive deterministic permutations and negative cases.

Representative E1002 evidence for B, C, D, E, and G opens the production connectivity lifecycle integration prerequisite for this epoch/fence model. A and F remain environment-limited because the current AP cannot provide controlled first-attempt failure/timeout conditions, and they must not be marked hardware-passed until executed. This does not close full E1002 board validation, E1004 validation, or release validation.
