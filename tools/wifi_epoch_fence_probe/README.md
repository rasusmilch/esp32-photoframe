# Wi-Fi epoch-fence probe

This standalone ESP-IDF application tests a **provisional** physical-attempt barrier. It is not linked into production firmware and cannot establish safety until the physical matrix passes. Traces never contain credentials.

## Ownership, synchronization, and fence

One owner task exclusively writes the active context and calls Wi-Fi mode, configuration, start, connect, and stop APIs. Wi-Fi, IP, and fence callbacks take an internally consistent snapshot under the same short `portMUX_TYPE` critical section used by owner writes; no lock covers a driver call, event post, queue send, or trace print, and 64-bit access is never assumed atomic.

On stop, the owner marks the immutable epoch/attempt/generation/owner context stopping and calls `esp_wifi_stop()` once. The application STA-stop handler enqueues its synchronized snapshot and posts a copied payload of that exact snapshot to the back of the default event loop. The fence handler enqueues that payload. Only the owner may compare it with the still-stopping context, trace the fence, and release the slot; only a later owner iteration may start another epoch. A stop return, disconnect, delay, yield, or cancellation request is not a fence.

Queue overflow, fence-post failure, snapshot mismatch, driver failure, repeated stop, start overlap, and fence timeout latch a callback-safe probe fault. The owner emits the fault when it can run, does not release unsafe state, and cannot emit successful completion. Initialization failures before tracing is available remain an explicit limitation.

## Trace and completion

Each `EPOCH_TRACE ` JSON line includes run, monotonic sequence/time, source, raw base and numeric ID, symbolic event, epoch, attempt ID, generation, owner, adapter state, mode, disconnect reason, action, and safe result. Semantic names are `sta_start`, `sta_stop`, `sta_connected`, `sta_disconnected`, `got_ip`, `lost_ip`, `wifi_other`, `ip_other`, and `fence_dispatched`. Unknown IDs retain their raw ID. Other monitor lines are ignored.

A successful run ends only after every epoch has STA-stop, matching fence, and release records, no fault or pending start exists, and exactly one `run_complete` is emitted. Scenario E first observes APSTA-to-STA behavior for the bounded owner timeout, then stops/fences/releases. A capture ending before `run_complete` fails.

Scenarios are: 1 failed/timed-out attempt followed by one completed retry; 2 generation-2 replacement while generation 1 is active; 3 distinct A→B and B→C updates while A remains active, then only C starts; 4 explicitly configured AP+STA portal failure, response marker, fenced AP restart, and final fence; 5 explicitly configured AP+STA, real GOT_IP, persistence/response markers, STA-mode transition observation, and final fence; 6 one-shot timeout stop; 7 configuration-API replacement through the same owner. Callbacks never reconnect and no production credentials are persisted.

## Offline checker

Run `make test-wifi-epoch-trace`, or:

    python3 tools/wifi_epoch_fence_probe/check_trace.py capture.log

The checker enforces increasing sequence/nondecreasing time, unique IDs, one owner, exact STA-stop/fence/release ordering and context, symbolic event semantics, no post-fence driver event, no hidden reconnect, no evidence-loss fault, and terminal completion. Synthetic tests require every named negative fixture to fail for its intended reason and deliberately truncate every passing fixture.

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
