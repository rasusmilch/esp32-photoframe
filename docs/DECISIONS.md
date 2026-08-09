# Decision log

Entries are append-only. A changed decision receives a new entry that explicitly supersedes the old one.

All entries below are **Accepted**, dated 2026-07-28, and require traceability through `docs/VALIDATION.md`.

| ID | Decision and context | Consequences | Requirements / validation obligation |
|---|---|---|---|
| DEC-001 | Local slideshow is offline-first and independently functional. | Cold boot/display/navigation/schedule cannot depend on Wi-Fi. | REQ-OFFLINE-*; offline matrices |
| DEC-002 | Wi-Fi is additive, not a local-mode prerequisite. | Network services cannot own local lifecycle. | REQ-WIFI-001, REQ-COMPAT-*; boot tests |
| DEC-003 | Retry is non-destructive, serialized, and background. | Preserve credentials; no overlap/reboot loop. | REQ-WIFI-003/004; fake-clock/failure tests |
| DEC-004 | Default retry is configurable and 15 minutes. | Configuration and scheduling require tests. | REQ-WIFI-002 |
| DEC-005 | `wifi.txt` is deleted only after durable commit/verification. | Recover deletion/power failures without repeat import. | REQ-CREDENTIAL-*; fault injection |
| DEC-006 | Preserve all six current boards. | No silent feature/board removal. | REQ-PLATFORM-001/003; build/hardware matrices |
| DEC-007 | E1002/E1004 are priority targets. | First complete physical matrices target them. | REQ-PLATFORM-002 |
| DEC-008 | Controls mean refresh, previous, next, and long-press clear as mapped in hardware authority. | Insufficient controls may be explicit; no board removal. | REQ-CONTROL-*, REQ-NAV-*; button tests |
| DEC-009 | Physical inputs are separate from semantic actions. | HAL role names cannot dictate global policy. | REQ-CONTROL-002; architecture review |
| DEC-010 | Direct SD JPEG discovery is deferred. | BMP/PNG/EPDGZ local; JPEG uses upload/conversion. | REQ-IMAGE-001 |
| DEC-011 | Preserve URL, HA, OTA, web/API, mDNS, and conversion workflows. | Offline changes require compatibility checks. | REQ-COMPAT-* |
| DEC-012 | Secrets are redacted while safe reason diagnostics remain. | Never log credentials/forms; log disconnect reasons. | REQ-SEC-*, REQ-WIFI-005; log tests |
| DEC-013 | Codex receipts use exactly one `~~~text` fence with no nested fence. | Receipt formatting is repository authority. | `AGENTS.md`; receipt review |
| DEC-014 | The available local checkout is authoritative when network/GitHub is unavailable. | Work proceeds using inspected local evidence. | `AGENTS.md`; inspection receipt |
| DEC-015 | Local/remote SHA differences are expected and do not independently block work. | Record the actual local SHA used. | `AGENTS.md`; validation metadata |
| DEC-016 | Standalone epoch-fence physical validation uses one accepted representative execution per scenario, with diagnostic reruns only after failure, anomaly, timing sensitivity, relevant implementation change, or intentionally different environment; supersedes arbitrary repeated-identical-run requirements. | B/C/D/E/G accepted E1002 evidence opens the production connectivity lifecycle integration prerequisite while A/F remain environment-limited debt; failed C1/E1 runs remain evidence. This is not full E1002/E1004 hardware validation or release closure. | `docs/VALIDATION.md`; host synthetic A–G coverage; external physical evidence provenance |
| DEC-017 | Production Wi-Fi lifecycle operations have one serialized owner adapting the pure connectivity policy and validated stop/fence/quarantine model. | Normal boot, provisioning, scan, replacement, performance mode, and sleep teardown submit through `connectivity_runtime`; compatibility APIs publish state but cannot become alternative driver owners. | REQ-WIFI-001/002/003/004/005, REQ-CREDENTIAL-001, REQ-PROVISION-003, DEC-002/003/016; host lifecycle tests; production hardware validation pending |
