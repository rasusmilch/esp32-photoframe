# Operations, provisioning, and recovery

This guide distinguishes **current behavior** at the adoption snapshot from **accepted target behavior**. Do not rely on target behavior until its implementation is validated.

## Local storage and offline use

Prepare enabled albums under `/storage/images`; the firmware creates/uses a default album through its album manager. Local discovery currently accepts BMP, PNG, and EPDGZ. Direct SD JPEG discovery is deferred: use the web upload processor or `process-cli` conversion workflow.

**Current limitation:** timer/rotate deep-sleep storage paths can rotate without Wi-Fi, but normal cold boot currently waits for provisioning or a successful connection; ordinary button handling also starts after Wi-Fi success. **Accepted target:** cold boot, retained display, scheduled rotation, refresh/previous/next, and recovery controls continue without Wi-Fi. Network retry runs serialized in the background every 15 minutes by default and must not replace valid displayed content.

## Captive portal

The portal is an operator provisioning channel, not a prerequisite for local slideshow. Target parsing receives the full bounded request across partial receives, accepts field order variations, strictly decodes form encoding, and validates decoded WPA passphrases through 63 bytes. Malformed input must produce no partial activation. Never include passwords or complete request bodies in diagnostics.

## `wifi.txt` import

Current source searches `/storage/config/wifi.txt` before `/storage/wifi.txt` on SD-capable boards. The file is positional: line 1 is a non-empty SSID (maximum 31 bytes), line 2 is a required password line that may be empty (maximum 63 bytes), and line 3 is an optional device name (maximum 63 bytes). LF and CRLF are accepted; spaces and a leading `#` are data. An absent or empty third line leaves the current device name unchanged. Up to four trailing empty CRLF lines fit the derived 169-byte ceiling; extra non-empty fields, malformed line endings, NUL, truncation, and overflow are rejected.

Implemented and host-verified consume-once sequence (ESP-IDF adapter, physical NVS/SD, and hardware validation remain pending):

1. Read the complete exact file into a fixed buffer and parse all fields without mutation.
2. Load the active NVS profile; omitted device name inherits its current value.
3. If different, set SSID, password, and effective device name through one NVS handle and commit once.
4. Reopen read-only and verify every field exactly, then publish in-memory caches.
5. Delete only the selected source path after verification. No connection test or import restart occurs.
6. If deletion fails, the retained file compares equal on the next boot and is deletion-only recovery: credentials are not rewritten and no restart is requested.

Malformed, incomplete, unreadable, oversized, uncommitted, or unverifiable candidates retain the file and are never deleted by the coordinator. Pre-commit failures preserve the prior profile. A valid file is checked before Wi-Fi/provisioning initialization even when credentials already exist, so it can explicitly replace them and its verified device identity is active for same-boot hostname setup. If only one stored SSID/password key exists, a valid file repairs the incomplete pair through the normal single commit/readback flow; without a valid source the partial state is left untouched for provisioning recovery.

## Unavailable or changed access point

**Current limitation:** normal startup currently clears credentials and restarts after a failed connection. **Accepted target:** retain last-known-good credentials, do not repeatedly restart, keep local operation/buttons active, serialize retry attempts, and retry at the configurable interval (15 minutes default). Credential replacement is an explicit operator action through a successfully validated portal/import; transient failure is not replacement.

Factory reset is deliberately destructive and erases persistent configuration. Use the documented `idf.py erase-flash` only when that outcome is intended; see `docs/DEV.md`. Automatic recovery must never be described as factory reset.

## Controls and recovery

The accepted mappings are canonical in `docs/HARDWARE.md`. They must remain available during attempts, provisioning, and failure. Current generic awake actions differ, so do not claim previous or long-press-clear is implemented. Short presses, bounce, and a held wake must never count as long clear.

## Flash, monitor, logs, and power

Use commands and status labels in `docs/DEV.md`; their presence does not prove execution. Before diagnosing sleep/USB behavior, identify the exact board and E1002 revision and consult `docs/HARDWARE.md`.

Collect disconnect reason, state transitions, reset/wake cause, and timestamps. Redact passwords, tokens, authorization/secret headers, complete form bodies, credential files, and private URLs containing secrets before sharing logs.
