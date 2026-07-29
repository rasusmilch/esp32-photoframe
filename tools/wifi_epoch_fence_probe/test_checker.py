#!/usr/bin/env python3
import json
import unittest
from pathlib import Path

from check_trace import check_records

ROOT = Path(__file__).parent


class CheckerFixtures(unittest.TestCase):
    def test_passing_fixtures_complete_and_truncation_fails(self):
        for path in sorted((ROOT / "traces").glob("pass_*.jsonl")):
            lines = path.read_text().splitlines()
            self.assertTrue(any('"action":"run_complete"' in line for line in lines), path.name)
            failures, count = check_records(lines)
            self.assertGreater(count, 0, path.name)
            self.assertEqual([], failures, path.name)
            truncated, _ = check_records(lines[:-1])
            self.assertTrue(truncated, f"truncated {path.name}")

    def test_each_failure_has_intended_reason(self):
        expected = json.loads((ROOT / "expected_failures.json").read_text())
        self.assertGreaterEqual(len(expected), 20)
        for name, reason in expected.items():
            failures, _ = check_records((ROOT / "traces" / name).read_text().splitlines())
            messages = [message for _, message in failures]
            self.assertTrue(any(reason in message for message in messages),
                            f"{name}: expected {reason!r}, got {messages!r}")


if __name__ == "__main__":
    unittest.main()
