#!/usr/bin/env python3
"""Strict standard-library validator for EPOCH_TRACE JSON-lines captures."""

import argparse
import json
import sys
from pathlib import Path

PREFIX = "EPOCH_TRACE "
REQUIRED = {"run", "seq", "ts_ms", "source", "base", "event_id", "event", "epoch",
            "attempt_id", "generation", "owner", "state", "mode", "reason", "action",
            "result"}
FORBIDDEN = {"ssid", "password", "token", "authorization", "body", "credentials"}
DRIVER_EVENTS = {"sta_start", "sta_stop", "sta_connected", "sta_disconnected", "got_ip",
                 "lost_ip", "wifi_other", "ip_other"}


def check_records(lines):
    failures, runs, records = [], {}, 0

    def fail(line, message):
        failures.append((line, message))

    for line_no, line in enumerate(lines, 1):
        if PREFIX not in line:
            continue
        try:
            rec = json.loads(line.split(PREFIX, 1)[1].strip())
        except json.JSONDecodeError as exc:
            fail(line_no, f"invalid JSON: {exc.msg}")
            continue
        records += 1
        missing = REQUIRED - rec.keys()
        if missing:
            fail(line_no, f"missing fields: {sorted(missing)}")
            continue
        secret = FORBIDDEN & {str(key).lower() for key in rec}
        if secret:
            fail(line_no, f"secret fields present: {sorted(secret)}")
        run = runs.setdefault(rec["run"], {"seq": -1, "ts": -1, "active": None,
                                           "epochs": {}, "attempts": set(), "complete": 0,
                                           "fault": False, "last_line": line_no,
                                           "ap_configured": False})
        if run["complete"]:
            fail(line_no, "trace record after run_complete")
        if rec["seq"] <= run["seq"]:
            fail(line_no, "trace sequence is not strictly increasing")
        if rec["ts_ms"] < run["ts"]:
            fail(line_no, "timestamp moved backwards")
        run["seq"], run["ts"], run["last_line"] = rec["seq"], rec["ts_ms"], line_no
        action, event, epoch = rec["action"], rec["event"], rec["epoch"]
        context = (rec["epoch"], rec["attempt_id"], rec["generation"], rec["owner"])
        if (epoch in run["epochs"] and action != "desired_generation_update" and
                context != run["epochs"][epoch]["context"]):
            fail(line_no, "physical epoch context changed or ownership overlap")

        if action == "probe_fault" or (action in {"post_fence", "queue_send", "driver_call"}
                                       and rec["result"] != "ok"):
            run["fault"] = True
            fail(line_no, f"probe fault: {event}")
        if action == "epoch_start":
            if run["active"] is not None:
                fail(line_no, "new physical epoch before prior release")
            if epoch in run["epochs"]:
                fail(line_no, "epoch reused")
            if rec["attempt_id"] in run["attempts"]:
                fail(line_no, "attempt ID reused")
            run["epochs"][epoch] = {"context": context, "stop": False, "fence": False,
                                     "release": False, "stopping": False}
            run["attempts"].add(rec["attempt_id"])
            run["active"] = epoch
        elif event in DRIVER_EVENTS:
            if epoch == 0 or epoch not in run["epochs"]:
                fail(line_no, "driver event has no attributable physical context")
            elif run["epochs"][epoch]["fence"]:
                fail(line_no, "old-epoch Wi-Fi/IP event after fence")
            if event == "sta_stop" and epoch in run["epochs"]:
                if context != run["epochs"][epoch]["context"]:
                    fail(line_no, "STA_STOP context mismatch")
                run["epochs"][epoch]["stop"] = True
            if event == "got_ip" and epoch in run["epochs"]:
                if rec["generation"] != run["epochs"][epoch]["context"][2]:
                    fail(line_no, "GOT_IP assigned to wrong generation")
        if action == "stop_requested" and epoch in run["epochs"]:
            if run["epochs"][epoch]["stopping"]:
                fail(line_no, "repeated stop request while stopping")
            run["epochs"][epoch]["stopping"] = True
        if action == "callback_reconnect":
            fail(line_no, "hidden callback reconnect")
        if action in {"cancel_ack", "retry_start"} and epoch in run["epochs"] and not run["epochs"][epoch]["fence"]:
            fail(line_no, f"{action} occurred before fence")
        if action == "ap_configured" and rec["result"] == "ok":
            run["ap_configured"] = True
        if rec["owner"] == "portal" and action == "sta_configured" and not run["ap_configured"]:
            fail(line_no, "APSTA scenario lacks prior AP configuration")
        if action in {"fence_observed", "epoch_release"}:
            if epoch not in run["epochs"]:
                if action == "fence_observed" and run["active"] in run["epochs"]:
                    fail(line_no, "fence context mismatch")
                fail(line_no, f"{action} for unknown epoch")
            else:
                expected = run["epochs"][epoch]["context"]
                if context != expected:
                    fail(line_no, "fence context mismatch")
                if action == "fence_observed":
                    if not run["epochs"][epoch]["stop"]:
                        fail(line_no, "fence observed before STA_STOP")
                    run["epochs"][epoch]["fence"] = True
                else:
                    if not run["epochs"][epoch]["fence"]:
                        fail(line_no, "physical slot released before fence")
                    run["epochs"][epoch]["release"] = True
                    if run["active"] != epoch:
                        fail(line_no, "released epoch is not active")
                    run["active"] = None
        if action == "desired_generation_update" and rec.get("generation") == 2 and rec.get("result") == "started":
            fail(line_no, "scenario C started generation 2")
        if action == "run_complete":
            run["complete"] += 1
            if run["complete"] != 1:
                fail(line_no, "duplicate run_complete")
            if run["active"] is not None:
                fail(line_no, "run_complete while epoch active")
            if run["fault"]:
                fail(line_no, "run_complete after probe fault")
            for value in run["epochs"].values():
                if not (value["stop"] and value["fence"] and value["release"]):
                    fail(line_no, "run_complete with incomplete epoch")
    if records == 0:
        fail(0, "no trace records found")
    for run_id, run in runs.items():
        if run["complete"] != 1:
            fail(run["last_line"], f"run {run_id} missing run_complete")
        if run["active"] is not None:
            fail(run["last_line"], f"run {run_id} ended with active epoch")
        for epoch, value in run["epochs"].items():
            if not value["stop"]:
                fail(run["last_line"], f"epoch {epoch} ended before STA_STOP")
            elif not value["fence"]:
                fail(run["last_line"], f"epoch {epoch} ended before fence")
            elif not value["release"]:
                fail(run["last_line"], f"epoch {epoch} ended before release")
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
