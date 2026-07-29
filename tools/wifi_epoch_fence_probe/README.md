# Wi-Fi epoch-fence probe

This standalone ESP-IDF application tests a **provisional** physical-attempt barrier. It is not
linked into production firmware and does not establish that the barrier is safe until the physical
matrix below passes. Trace records never contain credentials.

## Model and trace

One owner task exclusively changes mode/configuration and starts, connects, stops, replaces, or
tests STA. Event callbacks only snapshot the immutable active `{epoch, attempt_id, generation,
owner}` context and enqueue bounded records. Stop marks that context stopping, calls
`esp_wifi_stop()`, waits for application `WIFI_EVENT_STA_STOP`, posts `PROBE_FENCE_EVENT` to the
back of the default loop, and retains the slot until the owner receives the custom event. A later
owner iteration alone may start another epoch. A failed fence post holds the slot.

Lines beginning `EPOCH_TRACE ` contain JSON with: run, monotonic sequence/time, source, raw base
and ID, stable event name, epoch, attempt ID, credential generation, owner (`coordinator`, `portal`,
or `none`), adapter state, Wi-Fi mode, raw disconnect reason, action, and result. Other monitor
lines are ignored by the checker.

Selectable `CONFIG_PROBE_SCENARIO` values are: 1 failed connection/retry; 2 replacement; 3 rapid
A→B→C replacement; 4 captive APSTA failure/restart; 5 captive APSTA success and APSTA→STA mode
observation; 6 timeout; 7 configuration-API replacement through the same owner. The probe never
reconnects in a callback. Scenario 5 records response/persistence simulation markers before the
mode change; it does not write production credentials.

## Offline checker

Run `make test-wifi-epoch-trace`, or check a capture directly:

    python3 tools/wifi_epoch_fence_probe/check_trace.py capture.log

The checker rejects pre-fence new epochs, simultaneous/portal-coordinator ownership, post-fence
old Wi-Fi/IP events, wrong-generation GOT_IP, pre-fence cancellation acknowledgement/retry,
callback reconnect, unattributed driver events, reused epochs/attempt IDs, premature release/fence,
and secret fields. It supports multiple run IDs and unrelated log lines.

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
