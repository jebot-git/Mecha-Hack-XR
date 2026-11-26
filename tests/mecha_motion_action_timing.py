import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.mecha_motion.action_timing import GRID, fit, retime, timing_target


class ActionTimingTests(unittest.TestCase):
    def test_event_locked_monotone_remap(self):
        curve = GRID + 0.02 * np.sin(2 * np.pi * GRID)
        anchors = [0.18, 0.22, 0.41, 0.72]
        t = np.linspace(0, 1, 4001)
        result = retime(t, curve, anchors)
        np.testing.assert_allclose(
            retime([0.0, *anchors, 1.0], curve, anchors),
            [0.0, *anchors, 1.0],
            atol=1e-14,
        )
        self.assertGreater(np.diff(result).min(), 0)
        self.assertGreater(np.max(np.abs(result - t)), 0.001)
        with self.assertRaises(ValueError):
            retime(t, curve[::-1], anchors)

    def test_constant_motion_and_quaternion_signs(self):
        pose = np.zeros((40, 2, 7))
        pose[:, :, 3] = 1
        pose[::2, :, 3] = -1
        root = np.zeros((len(pose), 3))
        root[:, 0] = np.linspace(0, 1, len(pose))
        pivots = np.array([[0.0, 0, 0], [1.0, 2, 0]])
        np.testing.assert_allclose(timing_target(pose, root, pivots, 4.0), GRID)
        root[:, 0] = np.clip(np.linspace(-1, 2, len(pose)), 0, 1)
        target = timing_target(pose, root, pivots, 4.0)
        self.assertGreater(np.max(np.abs(target - GRID)), 0.004)
        self.assertLessEqual(np.max(abs(target - GRID)) * 4, 0.075001)

    def test_fit_and_unseen_rig_holdout(self):
        target = GRID + 0.025 * np.sin(2 * np.pi * GRID)
        data = {
            "mecha/cut": [(name, target) for name in ("a", "a", "b", "c")],
            "kaiju/burrow": [("wurm", target)],
        }
        result = fit(data, "cpu", 80)
        self.assertEqual(result["fit_clips"] + result["held_rig_clips"], 5)
        for metrics in result["metrics"].values():
            self.assertLess(metrics["phase_rmse"], 0.1 * metrics["uniform_phase_rmse"])
        self.assertEqual(set(result["curves"]), set(data))


if __name__ == "__main__":
    unittest.main()
