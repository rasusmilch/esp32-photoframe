# Operations, provisioning, and recovery

This guide distinguishes **current behavior** at the adoption snapshot from **accepted target behavior**. Do not rely on target behavior until its implementation is validated.

## Local storage and offline use

Prepare enabled albums under `/storage/images`; the firmware creates/uses a default album through its album manager. Local discovery currently accepts BMP, PNG, and EPDGZ. Direct SD JPEG discovery is deferred: use the web upload processor or `process-cli` conversion workflow.

**Current limitation:** timer/rotate deep-sleep storage paths can rotate without Wi-Fi, but normal cold boot currently waits for provisioning or a successful connection; ordinary button handling also starts after Wi-Fi success. **Accepted target:** cold boot, retained display, scheduled rotation, refresh/previous/next, and recovery controls continue without Wi-Fi. Network retry runs serialized in the background every 15 minutes by default and must not replace valid displayed content.

## Captive portal

The portal is an operator provisioning channel, not a prerequisite for local slideshow. Target parsing receives the full bounded request across partial receives, accepts field order variations, strictly decodes form encoding, and validates decoded WPA passphrases through 63 bytes. Malformed input must produce no partial activation. Never include passwords or complete request bodies in diagnostics.

## `wifi.txt` import

Current source searches `/storage/config/wifi.txt` before `/storage/wifi.txt`. At the adoption snapshot it reads but does not delete the file and may restart after saving; this is a known gap.

Target consume-once sequence:

1. Read the complete exact file and parse all candidate fields without persistence.
2. Validate credentials and optional device/network settings.
3. Stage and durably save them, commit, and verify as required.
4. Delete only the exact successfully imported source.
5. If deletion fails after commit, suppress repeat import/restart through recoverable committed state.

Malformed, incomplete, uncommitted, or failed candidates must retain both the file for operator correction and prior valid credentials. Optional device name must not be partially persisted.

## Unavailable or changed access point

**Current limitation:** normal startup currently clears credentials and restarts after a failed connection. **Accepted target:** retain last-known-good credentials, do not repeatedly restart, keep local operation/buttons active, serialize retry attempts, and retry at the configurable interval (15 minutes default). Credential replacement is an explicit operator action through a successfully validated portal/import; transient failure is not replacement.

Factory reset is deliberately destructive and erases persistent configuration. Use the documented `idf.py erase-flash` only when that outcome is intended; see `docs/DEV.md`. Automatic recovery must never be described as factory reset.

## Controls and recovery

The accepted mappings are canonical in `docs/HARDWARE.md`. They must remain available during attempts, provisioning, and failure. Current generic awake actions differ, so do not claim previous or long-press-clear is implemented. Short presses, bounce, and a held wake must never count as long clear.

## Flash, monitor, logs, and power

Use commands and status labels in `docs/DEV.md`; their presence does not prove execution. Before diagnosing sleep/USB behavior, identify the exact board and E1002 revision and consult `docs/HARDWARE.md`.

Collect disconnect reason, state transitions, reset/wake cause, and timestamps. Redact passwords, tokens, authorization/secret headers, complete form bodies, credential files, and private URLs containing secrets before sharing logs.
