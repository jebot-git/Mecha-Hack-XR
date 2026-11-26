"""Regression checks for the accepted rigid elbow and kaiju bite retargets."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np
from scipy.spatial.transform import Rotation

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import itertools

from mecha_motion.bake import bake
from mecha_motion.glb import export_clip
from mecha_motion.rigs import discover
from mecha_motion.verify import check_glb


class CharacterRetargetTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pack = ROOT / "build-mecha/game-assets/mecha"
        cls.rigs, _ = discover(cls.pack)
        ref = ROOT / "art/mecha/motion-evaluation"
        gait = json.loads((ref / "kimodo-gait.json").read_bytes())["sides"]
        brace = json.loads((ref / "reference/brace.json").read_bytes())
        cls.curves = {s: gait[s]["swing_clearance"] for s in ("left", "right")}
        cls.curves.update(
            depth=brace["depth"],
            shoulder=brace["shoulder"],
            sway=json.loads((ref / "reference/kimodo-sway.json").read_bytes())["lateral"],
        )

    def sample(self, model):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        out = Path(tmp.name)
        bake(self.pack, model, self.rigs[model], self.curves, out / "clip.json")
        clip = json.loads((out / "clip.json").read_bytes())
        export_clip(self.pack, clip, out / "clip.glb")
        check_glb(out / "clip.glb", clip)
        return clip

    def test_ace_forearms_carry_equipment(self):
        for model in ("mecha_ace", "mecha_ace_alt"):
            with self.subTest(model=model):
                clip = self.sample(model)
                parts = clip["parts"]
                poses = np.asarray(clip["frames"])
                self.assertEqual(len(parts), 14)
                index = {p["name"]: i for i, p in enumerate(parts)}
                for side in (-1, 1):
                    wrist = index[f"forearm_{side}"]
                    arm = index[f"arm_{side}"]
                    self.assertEqual(parts[wrist]["parent"], arm)
                    fore = Rotation.from_quat(poses[:, wrist, :4])
                    upper = Rotation.from_quat(poses[:, arm, :4])
                    self.assertGreater(np.ptp((upper.inv() * fore).magnitude()), 0.45)
                barrel = index["barrel_1"]
                self.assertEqual(parts[barrel]["parent"], index["forearm_1"])
                np.testing.assert_allclose(poses[:, barrel], poses[:, index["forearm_1"]], atol=1e-6)
                self.assertLess(clip["metrics"]["max_mesh_penetration_m"], 0.0002)

    def test_stalker_jaw_closes_and_tail_lash_travels(self):
        clip = self.sample("kaiju_stalker")
        self.assertEqual(clip["action"], "coil-bite-recovery")
        poses = np.asarray(clip["frames"])
        peaks = []
        for i, p in enumerate(clip["parts"]):
            if p["role"] not in ("tail", "jaw"):
                continue
            parent = Rotation.from_quat(poses[:, p["parent"], :4])
            local = parent.inv() * Rotation.from_quat(poses[:, i, :4])
            if p["role"] == "tail":
                peaks.append(np.argmin(local.as_euler("xyz")[89:, 1]) + 89)
            else:
                self.assertGreater(local[93].magnitude(), 0.45)
                self.assertLess(local[105].magnitude(), 0.01)
        self.assertGreaterEqual(len(peaks), 3)
        self.assertTrue(all(b > a for a, b in itertools.pairwise(peaks)))
        self.assertLess(clip["metrics"]["max_mesh_penetration_m"], 0.0002)
        self.assertLess(clip["metrics"]["max_contact_speed_mps"], 0.002)


if __name__ == "__main__":
    unittest.main()
