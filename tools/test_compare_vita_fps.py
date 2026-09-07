import unittest
import json
import tempfile
from pathlib import Path
from unittest.mock import patch

from compare_vita_fps import compare, load


def capture(interval_us=50000, graphics_us=40000, clock=500):
    rows = []
    base = {"a": 0, "b": 0, "c": 0, "d": 0, "phase": 1, "map": 76,
            "mode": 2, "mode_copy": 2, "render": 1, "cutscene": 7, "game_frame": 0,
            "lag": 2, "automatic": 0, "paused": 0, "x": 0.0, "y": 0.0, "z": 0.0}
    for frame in range(120, 660):
        rows.append(dict(base, time_us=frame * interval_us, duration_us=0,
                         kind=1, sequence=frame, scene_timer=frame))
        rows.append(dict(base, time_us=frame * interval_us + 500, duration_us=graphics_us,
                         kind=2, sequence=frame, scene_timer=frame))
    rows.append(dict(base, time_us=0, duration_us=0, kind=4, sequence=0,
                     scene_timer=0, a=clock, b=222, c=222, d=166))
    metadata = {"schema": 1, "run": "test", "scenario": "attract", "complete": True,
                "valid": True, "events": len(rows), "profiles": 0, "dropped": 0,
                "dropped_profiles": 0, "profile_every": 0}
    return rows, metadata


class ComparisonTests(unittest.TestCase):
    def test_frame_throughput_and_submission_cost_are_separate(self):
        result = compare(*capture(), *capture(40000, 30000), "rap-crowd")
        self.assertEqual(result["reference"]["game_fps"], 20)
        self.assertEqual(result["candidate"]["game_fps"], 25)
        self.assertEqual(result["graphics_mean_reduction_percent"], 25)
        self.assertEqual(result["game_fps_improvement_percent"], 25)

    def test_clock_mismatch_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "clock"):
            compare(*capture(), *capture(clock=444), "rap-crowd")

    def test_sampled_profiles_are_rejected(self):
        rows, metadata = capture()
        metadata["profile_every"] = 32
        with self.assertRaisesRegex(ValueError, "profil"):
            compare(*capture(), rows, metadata, "rap-crowd")

    def test_compiled_profile_branches_are_not_a_quiet_control(self):
        rows, metadata = capture()
        metadata["compiled_stage_profiling"] = True
        with self.assertRaisesRegex(ValueError, "profil"):
            compare(*capture(), rows, metadata, "rap-crowd")

    def test_different_workload_coverage_is_rejected(self):
        rows, metadata = capture()
        rows = [r for r in rows if r["scene_timer"] != 140]
        metadata["events"] = len(rows)
        with self.assertRaisesRegex(ValueError, "coverage"):
            compare(*capture(), rows, metadata, "rap-crowd")

    def test_slowdown_is_reported_not_hidden(self):
        result = compare(*capture(), *capture(60000, 45000), "rap-crowd")
        self.assertLess(result["game_fps_improvement_percent"], 0)
        self.assertLess(result["graphics_mean_reduction_percent"], 0)

    def test_loading_a_post_capture_crash_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / "input.json").write_text(json.dumps({"run": "test"}))
            (path / "test.json").write_text(json.dumps(capture()[1]))
            (path / "lifecycle.json").write_text(json.dumps({"run": "test", "shutdown_complete": True,
                "new_core_dumps": ["psp2core-fault.psp2dmp"]}))
            with patch("compare_vita_fps.read_csv", return_value=capture()[0]):
                with self.assertRaisesRegex(ValueError, "crash dump"):
                    load(path)

    def test_historical_measurements_do_not_claim_clean_shutdown(self):
        result = compare(*capture(), *capture(40000, 30000), "rap-crowd")
        self.assertIs(result.get("clean_shutdown_verified"), False)


if __name__ == "__main__":
    unittest.main()
