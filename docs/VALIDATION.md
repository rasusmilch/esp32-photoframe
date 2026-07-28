# Validation ledger

## Rules and record schema

Do not fabricate results or promote README claims to evidence. Record the actual local SHA/branch; remote SHA difference is not failure. Compilation is not physical validation. Hardware revision is mandatory when relevant. Status is one of **passed**, **failed**, **environment-limited**, **pending**, or **not applicable**.

Every observation records: date; local SHA; branch; board/revision; firmware configuration; power source; storage/media contents; scenario; commands/procedure; expected and observed result; redacted artifact; validator; status; linked requirements/decisions; limitations.

## Adoption environment entry

| Date | Local SHA / branch | Scenario | Expected / observed | Validator | Status | Links / limitations |
|---|---|---|---|---|---|---|
| 2026-07-28 | `905c829949f7f8a30600b0011c02817217c1d59c` / `work` | Governance source inspection | Six boards and documented current gaps found; no runtime validation claimed | Codex | passed | DEC-014/015; local files only |
| 2026-07-28 | same | Firmware build and hardware validation | Not runnable / no ESP-IDF, network, board, or serial device | Codex | environment-limited | All runtime REQs; no physical evidence |

## Pending physical matrices

All rows below are **pending**, with SHA/configuration/revision/power/storage/procedure/artifact/validator to be filled only when executed.

### reTerminal E1002 — priority

Offline cold boot; unavailable AP; valid and malformed `wifi.txt`; post-commit deletion and deletion failure; maximum-length encoded portal password; refresh short/clear long; previous/next wrap; controls during retry/provisioning; offline timer wake; battery sleep/wake; USB detection by revision; RTC, SD, charger, and display behavior.

### reTerminal E1004 — priority

The same functional matrix, plus dual display chip selects, shared display/SD SPI, automatic-light-sleep disabled, adopted buttons, USB detection, RTC, charger, battery, sleep, and wake.

### Remaining boards

For Waveshare, XIAO EE02, XIAO EE04, and E1003: build status; GPIO/wake-mask inspection; available/unavailable controls; storage backend; timer wake; physical validation. All are pending. Waveshare dedicated previous is not applicable with current controls.
