#!/usr/bin/env python3
import unittest
from pathlib import Path

from check_trace import check_records


class CheckerFixtures(unittest.TestCase):
    def test_passing_fixtures(self):
        for path in sorted((Path(__file__).parent / "traces").glob("pass_*.jsonl")):
            failures, count = check_records(path.read_text().splitlines())
            self.assertGreater(count, 0, path.name)
            self.assertEqual([], failures, path.name)

    def test_failing_fixtures(self):
        paths = sorted((Path(__file__).parent / "traces").glob("fail_*.jsonl"))
        self.assertGreaterEqual(len(paths), 9)
        for path in paths:
            failures, _ = check_records(path.read_text().splitlines())
            self.assertTrue(failures, path.name)


if __name__ == "__main__":
    unittest.main()
