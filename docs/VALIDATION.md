# Validation ledger

## Rules and record schema

Do not fabricate results or promote README claims to evidence. Record the actual local SHA/branch; remote SHA difference is not failure. Compilation is not physical validation. Hardware revision is mandatory when relevant. Status is one of **passed**, **failed**, **environment-limited**, **pending**, or **not applicable**.

Every observation records: date; local SHA; branch; board/revision; firmware configuration; power source; storage/media contents; scenario; commands/procedure; expected and observed result; redacted artifact; validator; status; linked requirements/decisions; limitations.

## Adoption environment entry

| Date | Local SHA / branch | Scenario | Expected / observed | Validator | Status | Links / limitations |
|---|---|---|---|---|---|---|
| 2026-07-28 | `905c829949f7f8a30600b0011c02817217c1d59c` / `work` | Governance source inspection | Six boards and documented current gaps found; no runtime validation claimed | Codex | passed | DEC-014/015; local files only |
| 2026-07-28 | same | Firmware build and hardware validation | Not runnable / no ESP-IDF, network, board, or serial device | Codex | environment-limited | All runtime REQs; no physical evidence |
| 2026-07-28 | `5a11857edb220c94a53abdda4ff92892c75d889b` / `work` | Strict provisioning parser and exact-body reader | `make test-provisioning-form` and direct strict C11 compile both completed; all boundary tests passed | Codex | passed | REQ-PROVISION-001/002/003, REQ-SEC-001/002, DEC-012; host-only, no ESP-IDF adapter build |
| 2026-07-28 | `df7120a325c04971b6d6d5ac55f01bc8f950dd5b` / `work` | Pure `wifi.txt` parser, discovery, coordinator, and cache-publication boundary | `make test-wifi-import`, existing provisioning tests, and direct strict C11 compile completed; all focused tests passed | Codex | passed | REQ-CREDENTIAL-001/002/003, REQ-SEC-001/002, REQ-WIFI-004, DEC-005; host fakes only, ESP-IDF NVS/SD adapter and hardware not validated |
| 2026-07-29 | `6c4608b51c39c6d40f24de1141e9628b6da80ed9` / `work` | Same-boot import ordering and incomplete-profile coordinator recovery | `make test-wifi-import`, `make test-provisioning-form`, and direct strict C11 import compile completed; all focused tests passed; source review placed normal-path import before Wi-Fi/provisioning initialization | Codex | passed | REQ-CREDENTIAL-001/002/003, REQ-WIFI-004, DEC-005; pure coordinator and source-order review only—ESP-IDF, DHCP hostname, NVS/SD, and hardware remain unvalidated |
| 2026-07-29 | `f62ed3abb4a918df28e97f634b2cdedbaca55fad` / `work` | Pure normal-boot and serialized connectivity-retry policy | All three focused make targets and a direct strict C11 connectivity-policy compile/run passed | Codex | passed | REQ-OFFLINE-001/002/003, REQ-WIFI-001/002/003/004, REQ-CONTROL-001, REQ-COMPAT-001/002; pure policy only, runtime/ESP-IDF/network/hardware unvalidated |

## Pending physical matrices

All rows below are **pending**, with SHA/configuration/revision/power/storage/procedure/artifact/validator to be filled only when executed.

### reTerminal E1002 — priority

Offline cold boot; unavailable AP; valid and malformed `wifi.txt`; post-commit deletion and deletion failure; maximum-length encoded portal password; refresh short/clear long; previous/next wrap; controls during retry/provisioning; offline timer wake; battery sleep/wake; USB detection by revision; RTC, SD, charger, and display behavior.

### reTerminal E1004 — priority

The same functional matrix, plus dual display chip selects, shared display/SD SPI, automatic-light-sleep disabled, adopted buttons, USB detection, RTC, charger, battery, sleep, and wake.

### Remaining boards

For Waveshare, XIAO EE02, XIAO EE04, and E1003: build status; GPIO/wake-mask inspection; available/unavailable controls; storage backend; timer wake; physical validation. All are pending. Waveshare dedicated previous is not applicable with current controls.
