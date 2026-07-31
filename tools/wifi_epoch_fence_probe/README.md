# Wi-Fi epoch-fence probe

This standalone ESP-IDF application tests a **provisional** physical-attempt barrier. It is not linked into production firmware and cannot establish safety until the physical matrix passes. Traces never contain credentials.

## Ownership, synchronization, and fence

One owner task exclusively writes the active context and calls Wi-Fi mode, configuration, start, connect, and stop APIs. Wi-Fi, IP, and fence callbacks take an internally consistent snapshot under the same short `portMUX_TYPE` critical section used by owner writes; no lock covers a driver call, event post, queue send, or trace print, and 64-bit access is never assumed atomic.

On stop, the owner marks the immutable epoch/attempt/generation/owner/requested-mode identity stopping and calls `esp_wifi_stop()` once. Separate synchronized evidence requires mask 1 (STA stop) for STA, mask 2 (AP stop) for AP, and mask 3 (both) for APSTA. Inapplicable or duplicate stop events fail. A stop event while active is only a driver observation and cannot fence. Exactly one copied-context fence is posted only when the full required mask is observed; callbacks never release. The owner verifies identity and mask completion before accepting it. A stop return, disconnect, delay, yield, or cancellation request is not a fence.

Queue overflow, fence-post failure, snapshot mismatch, driver failure, repeated stop, start overlap, and fence timeout latch a callback-safe probe fault. The owner emits the fault when it can run, does not release unsafe state, and cannot emit successful completion. Initialization failures before tracing is available remain an explicit limitation.

## Trace and completion

Each `EPOCH_TRACE ` JSON line includes run, scenario/phase, `post_fence_observe_ms`, monotonic sequence/time, raw and symbolic event identity, physical identity, requested mode, required/observed stop masks, fence timestamp, absolute quarantine deadline, disconnect reason, action, and safe result. Semantic names are `sta_start`, `sta_stop`, `sta_connected`, `sta_disconnected`, `got_ip`, `lost_ip`, `wifi_other`, `ip_other`, and `fence_dispatched`. Unknown IDs retain their raw ID. Other monitor lines are ignored.

A matching fence records its monotonic timestamp and an overflow-checked absolute deadline, then begins a validation-only quiet quarantine without releasing context. Queue activity cannot reset the deadline; waits use the remaining duration with ceiling tick conversion. Any Wi-Fi/IP event before release fails. Completion is emitted only at or after the deadline, and the checker verifies elapsed time is at least `CONFIG_PROBE_POST_FENCE_OBSERVE_MS` (default 2000 ms, range 100–60000 ms). The owner then rechecks faults, releases, and schedules any next epoch later. This delay collects evidence and is not part of the proposed production barrier. A successful run ends only after its scenario-specific milestones and every epoch’s stop/fence/quarantine/release sequence, with exactly one `run_complete`.

Scenario state machines and phases are checked in epoch-qualified order: A requires a failed/timed-out first attempt and exactly one completed retry; B requires a generation-2 update before generation 1 releases; C requires ordered generation-2 and generation-3 updates but starts only generation 3; D requires a failed APSTA candidate, response marker, then a separately configured and observed AP-only epoch; E requires real GOT_IP, persistence and response markers, successful STA-mode request, stable observation without disconnect, then stop; F requires the configured timeout before its one stop; G requires configuration-API submission and generation replacement through the same owner. Every epoch records requested mode, interface configuration, outcome, stop request, complete mode-aware stop mask, fence, timed quarantine, and release. Scenario E deliberately changes its required stop mask from APSTA (3) to STA (1) only after a successful mode request: AP-stop during the active transition is recorded but cannot fence; the later explicit physical stop requires STA-stop. Callbacks never reconnect and no production credentials are persisted.

## Offline checker

Run `make test-wifi-epoch-trace`, or:

    python3 tools/wifi_epoch_fence_probe/check_trace.py capture.log

The checker enforces stable scenario identity, scenario-specific milestones and phase completion, increasing sequence/nondecreasing time, unique IDs, one owner, per-epoch AP/STA configuration, exact stop/fence/quarantine/release ordering and context, symbolic event semantics, no post-fence driver event, no hidden reconnect, no evidence-loss fault, and terminal completion. Synthetic tests require every named negative fixture to fail for its intended reason and deliberately truncate every passing fixture.

## Later ESP-IDF execution (pending, not run here)

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

## Required evidence before runtime integration resumes

- E1002: every scenario passes; replacement, timeout, APSTA failure, and rapid replacement each
  pass at least 25 repetitions; no post-fence old event, overlap, or lost fence.
- E1004: repeat the same matrix only after E1002 passes.
- Retain all traces and explicitly record APSTA→STA behavior.
- Failures remain failures; arbitrary delays are not an acceptable explanation or barrier.

Only those physical results may promote the stop/STA_STOP/default-loop-fence sequence from a
proposal to a runtime contract.
