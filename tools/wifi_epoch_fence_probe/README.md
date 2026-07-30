# Wi-Fi epoch-fence probe

This standalone ESP-IDF application tests a **provisional** physical-attempt barrier. It is not linked into production firmware and cannot establish safety until the physical matrix passes. Traces never contain credentials.

## Ownership, synchronization, and fence

One owner task exclusively writes the active context and calls Wi-Fi mode, configuration, start, connect, and stop APIs. Wi-Fi, IP, and fence callbacks take an internally consistent snapshot under the same short `portMUX_TYPE` critical section used by owner writes; no lock covers a driver call, event post, queue send, or trace print, and 64-bit access is never assumed atomic.

On stop, the owner marks the immutable epoch/attempt/generation/owner context stopping and calls `esp_wifi_stop()` once. The application STA-stop handler enqueues its synchronized snapshot and posts a copied payload of that exact snapshot to the back of the default event loop. The fence handler enqueues that payload. Only the owner may compare it with the still-stopping context, trace the fence, and release the slot; only a later owner iteration may start another epoch. A stop return, disconnect, delay, yield, or cancellation request is not a fence.

Queue overflow, fence-post failure, snapshot mismatch, driver failure, repeated stop, start overlap, and fence timeout latch a callback-safe probe fault. The owner emits the fault when it can run, does not release unsafe state, and cannot emit successful completion. Initialization failures before tracing is available remain an explicit limitation.

## Trace and completion

Each `EPOCH_TRACE ` JSON line includes run, selected scenario A–G, explicit scenario phase, selected `post_fence_observe_ms`, monotonic sequence/time, source, raw base and numeric ID, symbolic event, epoch, attempt ID, generation, owner, adapter state, requested mode, disconnect reason, action, and safe result. Semantic names are `sta_start`, `sta_stop`, `sta_connected`, `sta_disconnected`, `got_ip`, `lost_ip`, `wifi_other`, `ip_other`, and `fence_dispatched`. Unknown IDs retain their raw ID. Other monitor lines are ignored.

A matching fence begins a validation-only quiet quarantine; it does not release the context. The old context remains attributable for `CONFIG_PROBE_POST_FENCE_OBSERVE_MS` (default 2000 ms, configurable from 100–60000 ms). Any Wi-Fi/IP event during that interval is a failure. Only a quiet interval produces `post_fence_observation_complete`, after which the owner rechecks faults, releases, and schedules any next epoch on a later iteration. This delay collects evidence and is not part of the proposed production barrier. A successful run ends only after its scenario-specific milestones and every epoch’s stop/fence/quarantine/release sequence, with exactly one `run_complete`.

Scenario state machines are explicit: A requires a failed/timed-out first attempt and exactly one completed retry; B requires a generation-2 update before generation 1 releases; C requires ordered generation-2 and generation-3 updates but starts only generation 3; D requires a failed APSTA candidate, response marker, then a separately configured and observed AP-only epoch; E requires real GOT_IP, persistence and response markers, successful STA-mode request, stable observation without disconnect, then stop; F requires the configured timeout before its one stop; G requires configuration-API submission and generation replacement through the same owner. Every epoch records requested mode, AP/STA configuration as applicable, outcome, stop, stop event, fence, quarantine completion, and release. Callbacks never reconnect and no production credentials are persisted.

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
