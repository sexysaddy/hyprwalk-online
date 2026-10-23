#!/usr/bin/env python3
"""Process supervisor/marker unit tests ONLY: not a WebRTC simulation or pass."""
import tempfile
import pathlib
import unittest
from media_process_test import validate, run


def output(scenario="duplex", rounds=1, phases=1):
    return "".join("".join(f"PASS phase={p} name=case round={r} peer=test\n" for p in range(phases))
                   + f"WEBRTC_OK scenario={scenario} round={r} peer=test\n" for r in range(rounds))

class SupervisorTests(unittest.TestCase):
    def test_complete(self):
        self.assertIsNone(validate(output(), 0, "duplex"))
    def test_exit_required(self):
        self.assertIsNotNone(validate(output(), 1, "duplex"))
    def test_no_false_success_from_capability(self):
        self.assertIsNotNone(validate("capabilities=31 Streaming\n", 0, "duplex"))
    def test_first_phase_cannot_hide_later_failure(self):
        self.assertIsNotNone(validate(output("regrant"), 0, "regrant"))
    def test_all_phases_required(self):
        self.assertIsNone(validate(output("regrant", phases=3), 0, "regrant"))
    def test_duplicate_pass_rejected(self):
        self.assertIsNotNone(validate(output()+"PASS phase=0 name=case round=0 peer=test\n", 0, "duplex"))
    def test_skip_is_not_pass(self):
        self.assertIsNotNone(validate(output(), 77, "duplex"))
    def test_reconnect_requires_two_rounds(self):
        self.assertIsNotNone(validate(output("reconnect")+"RECONNECT_OK distinct session IDs\n", 0, "reconnect"))
    def test_reconnect_requires_scope_assertion(self):
        text=output("reconnect", rounds=2)
        self.assertIsNotNone(validate(text, 0, "reconnect"))
        self.assertIsNone(validate(text+"RECONNECT_OK distinct session IDs\n", 0, "reconnect"))
    def test_failed_assertion_not_hidden(self):
        self.assertIsNotNone(validate(output()+"FAIL second stream\n", 0, "duplex"))
    def test_scenario_validation_before_launch(self):
        with tempfile.TemporaryDirectory() as path:
            with self.assertRaises(ValueError):
                run("not-invoked", "not-invoked", pathlib.Path(path), ("invalid",))

if __name__ == "__main__":
    unittest.main(verbosity=2)
