# Repository operating contract

## Scope and inspection

This file applies to the whole repository unless a narrower nested `AGENTS.md` exists. Do not create a nested governance file without an accepted need. Inspect the latest available local source before planning, reviewing, changing, or validating work. When network access is unavailable, the local checkout is authoritative; local and remote SHAs may legitimately differ. Do not reject work solely because the directory is named `work`, the branch is not `main`, or a supplied remote SHA differs.

Source names, comments, tests, documentation, receipts, release notes, and historical behavior are claims to verify, not automatic authority.

## Authority and routing

Precedence is: this file; accepted requirements in `docs/GOVERNANCE.md`; `docs/HARDWARE.md`; architecture, operations, testing, and development policies; inspected source for observed implementation behavior; then historical README prose, changelog entries, decisions, validation records, and git history. Source describes what exists; it does not override an accepted requirement for what must change.

- Project intent: `README.md`
- Requirements, constraints, roadmap, deferred work, security: `docs/GOVERNANCE.md`
- Architecture and boundaries: `docs/ARCHITECTURE.md`
- Hardware contract: `docs/HARDWARE.md`
- Operations, deployment, provisioning, recovery, offline use: `docs/OPERATIONS.md`
- Testing strategy: `docs/TESTING.md`
- Build, format, review, docs, flash, monitor, code documentation: `docs/DEV.md`
- Decisions: `docs/DECISIONS.md`
- Validation: `docs/VALIDATION.md`
- Canonical changelog and `Unreleased`: `CHANGELOG.md`

Result vocabulary is **verified locally**, **CI-verified**, **hardware-validated**, **environment-limited**, **unknown**, and **explicitly absent**. A documented command is not verified merely because it exists; compilation is not hardware validation.

## Change rules

Update requirements when normative behavior changes; add or supersede significant policy/architecture decisions; record only observed validation; and update `CHANGELOG.md` in the same change for applicable user-visible, operational, security, compatibility, persistence, hardware, recovery, or significant governance changes. Update each domain document when its contract changes.

Before changing GPIOs, buttons, wake sources, display, storage buses, RTC, sensors, charger/PMIC, battery monitoring, USB detection, light sleep, or deep sleep, inspect the relevant board definitions and `docs/HARDWARE.md`. Never infer one board from another.

Never log passwords, tokens, complete credential files, authorization or secret custom headers, or complete provisioning bodies. Treat removable storage and captive-portal input as untrusted, validate before persistent activation, and preserve last-known-good credentials unless an operator explicitly resets or replaces them.

Stop when source and authority conflict in a way requiring a product decision, or a required artifact is missing. Do not invent policy. Report environment limitations rather than fabricating verification.

## Codex receipts

Every Codex receipt must be returned inside exactly one `~~~text` fence. Do not nest fenced blocks; represent commands, snippets, paths, and examples as indented or plain text.
