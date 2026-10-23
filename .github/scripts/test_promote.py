#!/usr/bin/env python3
"""python3 .github/scripts/test_promote.py"""
import os
import sys
import unittest

os.environ.setdefault("IMAGE", "ghcr.io/flyitalyadsb/readsb")
os.environ.update({"SOAK_HOURS": "20", "MIN_RATIO": "0.85", "MAX_DATA_AGE_S": "30"})
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import promote  # noqa: E402

NOW = 1_800_000_000.0
COMMIT = "ebc1bc589f5a6e97bfa8267d1fb2f951f4df086b"


def cell(commit="ebc1bc5", hours=21, aircraft=13000, mps=160000.0, age=1.0):
    return {"readsb_commit": commit, "readsb_started": NOW - hours * 3600,
            "aircraft": aircraft, "messages_per_s": mps, "data_age_s": age}


class Decide(unittest.TestCase):
    def test_promotes_a_canary_that_held_up(self):
        verdict, _ = promote.decide(COMMIT, {"a": cell(commit="0123abc"), "b": cell()}, NOW)
        self.assertEqual(verdict, "promote")

    def test_waits_while_not_deployed_or_still_soaking(self):
        self.assertEqual(promote.decide(COMMIT, {"a": cell(), "b": cell(commit="0123abc")}, NOW)[0], "wait")
        self.assertEqual(promote.decide(COMMIT, {"a": cell(), "b": cell(hours=3)}, NOW)[0], "wait")

    def test_waits_when_a_cell_is_missing(self):
        self.assertEqual(promote.decide(COMMIT, {"a": None, "b": cell()}, NOW)[0], "wait")
        self.assertEqual(promote.decide(COMMIT, {"a": cell(), "b": None}, NOW)[0], "wait")
        self.assertEqual(promote.decide(COMMIT, None, NOW)[0], "wait")

    def test_fails_when_b_falls_behind_or_goes_stale(self):
        for b in (cell(aircraft=9000), cell(mps=100000.0), cell(age=400.0), cell(age=None)):
            verdict, reason = promote.decide(COMMIT, {"a": cell(), "b": b}, NOW)
            self.assertEqual(verdict, "fail", reason)

    def test_a_small_gap_between_cells_is_normal(self):
        # Measured 2026-10-23: B 12,445 aircraft against A 13,261 (-6%).
        verdict, _ = promote.decide(COMMIT, {"a": cell(aircraft=13261), "b": cell(aircraft=12445)}, NOW)
        self.assertEqual(verdict, "promote")


if __name__ == "__main__":
    unittest.main(verbosity=2)
