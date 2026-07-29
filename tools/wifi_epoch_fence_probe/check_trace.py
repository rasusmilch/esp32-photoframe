#!/usr/bin/env python3
"""Validate EPOCH_TRACE JSON records using only the Python standard library."""

import argparse
import json
import sys
from pathlib import Path

PREFIX = "EPOCH_TRACE "
REQUIRED = {
    "run", "seq", "ts_ms", "source", "base", "event_id", "event", "epoch",
    "attempt_id", "generation", "owner", "state", "mode", "reason", "action", "result",
}
FORBIDDEN_KEYS = {"ssid", "password", "token", "authorization", "body", "credentials"}


def check_records(lines):
    failures = []
    runs = {}
    records = 0
    for line_no, line in enumerate(lines, 1):
        if PREFIX not in line:
            continue
        payload = line.split(PREFIX, 1)[1].strip()
        try:
            record = json.loads(payload)
        except json.JSONDecodeError as exc:
            failures.append((line_no, f"invalid JSON: {exc.msg}"))
            continue
        records += 1
        missing = REQUIRED - record.keys()
        forbidden = FORBIDDEN_KEYS & {key.lower() for key in record}
        if missing:
            failures.append((line_no, f"missing fields: {sorted(missing)}"))
            continue
        if forbidden:
            failures.append((line_no, f"secret fields present: {sorted(forbidden)}"))
        run = runs.setdefault(record["run"], {
            "seq": -1, "ts": -1, "active": None, "epochs": {}, "attempts": set(),
            "owners": set(),
        })
        if record["seq"] <= run["seq"]:
            failures.append((line_no, "trace sequence is not strictly increasing"))
        if record["ts_ms"] < run["ts"]:
            failures.append((line_no, "timestamp moved backwards"))
        run["seq"], run["ts"] = record["seq"], record["ts_ms"]
        epoch = record["epoch"]
        action = record["action"]
        base = record["base"]

        if action == "epoch_start":
            if run["active"] is not None:
                failures.append((line_no, "new physical epoch before prior fence/release"))
            if epoch in run["epochs"]:
                failures.append((line_no, "epoch reused"))
            if record["attempt_id"] in run["attempts"]:
                failures.append((line_no, "attempt ID reused"))
            run["epochs"][epoch] = {"generation": record["generation"], "fenced": False,
                                     "sta_stop": False}
            run["attempts"].add(record["attempt_id"])
            run["active"] = epoch
            run["owners"] = {record["owner"]}
        elif base in {"WIFI_EVENT", "IP_EVENT"}:
            if epoch == 0 or epoch not in run["epochs"]:
                failures.append((line_no, "driver event has no attributable physical context"))
            elif run["epochs"][epoch]["fenced"]:
                failures.append((line_no, "old-epoch Wi-Fi/IP event after fence"))
            if record["event"] == "sta_stop" or record["event_id"] == 3:
                if epoch in run["epochs"]:
                    run["epochs"][epoch]["sta_stop"] = True
            if record["event_id"] == 0 and base == "IP_EVENT" and epoch in run["epochs"]:
                if record["generation"] != run["epochs"][epoch]["generation"]:
                    failures.append((line_no, "GOT_IP assigned to wrong generation"))
        if action == "callback_reconnect":
            failures.append((line_no, "hidden callback reconnect"))
        if action in {"cancel_ack", "retry_start"} and epoch in run["epochs"] and not run["epochs"][epoch]["fenced"]:
            failures.append((line_no, f"{action} occurred before fence"))
        if action == "fence_observed":
            if epoch not in run["epochs"]:
                failures.append((line_no, "fence for unknown epoch"))
            else:
                if not run["epochs"][epoch]["sta_stop"]:
                    failures.append((line_no, "fence observed before STA_STOP"))
                run["epochs"][epoch]["fenced"] = True
        if action == "epoch_release":
            if epoch not in run["epochs"] or not run["epochs"][epoch]["fenced"]:
                failures.append((line_no, "physical slot released before fence"))
            if run["active"] != epoch:
                failures.append((line_no, "released epoch is not active"))
            run["active"] = None
            run["owners"].clear()
        if record["owner"] in {"portal", "coordinator"} and run["active"] == epoch:
            run["owners"].add(record["owner"])
            if len(run["owners"]) > 1:
                failures.append((line_no, "portal and coordinator ownership overlap"))
    if records == 0:
        failures.append((0, "no trace records found"))
    return failures, records


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("trace", type=Path)
    args = parser.parse_args(argv)
    failures, count = check_records(args.trace.read_text(encoding="utf-8").splitlines())
    if failures:
        for line, message in failures:
            print(f"line {line}: {message}", file=sys.stderr)
        return 1
    print(f"trace valid: {count} records")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
