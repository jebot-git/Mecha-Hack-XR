"""CPU regression tests for the optional PyTorch authoring tool."""

import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

import torch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from mecha_motion.training import LENGTHS, predict, train


class CurveTrainingTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.dir = Path(temp.name)
        self.ref = self.dir / "study/reference"
        self.ref.mkdir(parents=True)
        fixture = ROOT / "art/mecha/motion-evaluation"
        for filename in ("kimodo-sway.json", "brace.json"):
            shutil.copyfile(fixture / "reference" / filename, self.ref / filename)
        shutil.copyfile(fixture / "kimodo-gait.json", self.ref.parent / "kimodo-gait.json")

    def test_fit_resume_and_constraints(self):
        full = train(self.ref, self.dir / "one", epochs=400)
        train(self.ref.parent, self.dir / "two", epochs=73)
        resumed = train(self.ref, self.dir / "two", epochs=400, resume=True)
        curves = predict(full["checkpoint"])
        self.assertEqual(curves, predict(resumed["checkpoint"]))
        self.assertEqual(full["metrics"], resumed["metrics"])
        for key, values in curves.items():
            self.assertEqual(len(values), LENGTHS[key])
            self.assertGreater(full["metrics"][key]["validation_samples"], 0)
            self.assertLess(full["metrics"][key]["validation_rmse"], 0.07)
        self.assertEqual(curves["sway"][0], curves["sway"][-1])
        for key in ("left", "right"):
            self.assertEqual((curves[key][0], curves[key][-1]), (0, 0))
            self.assertGreater(max(curves[key]), 0.9)
        for key in ("depth", "shoulder"):
            self.assertEqual((curves[key][0], curves[key][-1]), (0, 1))
            self.assertEqual(curves[key], sorted(curves[key]))

    def test_extra_clips_and_checkpoint_provenance(self):
        baseline = train(self.ref, self.dir / "base", epochs=40)
        folder = self.ref / "training-clips"
        folder.mkdir()
        target = [0, 0.15, 0.25, 0.45, 0.7, 1, 0.8, 0.5, 0]
        (folder / "slow-step.json").write_text(json.dumps({"curves": {"left": target}}))
        with self.assertRaisesRegex(ValueError, "inputs changed"):
            train(self.ref, self.dir / "base", epochs=41, resume=True)
        fitted = train(self.ref, self.dir / "extra", epochs=40)
        self.assertNotEqual(predict(fitted["checkpoint"])["left"], predict(baseline["checkpoint"])["left"])
        (folder / "unseen-step.json").write_text(
            json.dumps({"split": "validation", "curves": {"left": target}})
        )
        scored = train(self.ref, self.dir / "held-out", epochs=40)
        self.assertEqual(predict(fitted["checkpoint"]), predict(scored["checkpoint"]))
        self.assertEqual(scored["metrics"]["left"]["validation_samples"], 13)
        self.assertEqual(scored["metrics"]["right"]["validation_samples"], 2)

    def test_selected_device(self):
        if hasattr(torch, "xpu") and torch.xpu.is_available():
            run = train(self.ref, self.dir / "xpu", "xpu", epochs=2)
            self.assertTrue(run["training_device"].startswith("xpu:"))
            self.assertEqual(set(predict(run["checkpoint"], "xpu")), set(LENGTHS))
        else:
            with self.assertRaisesRegex(RuntimeError, "Intel XPU was selected"):
                train(self.ref, self.dir / "xpu", "xpu", epochs=2)
            self.assertFalse((self.dir / "xpu/curve-adapter.pt").exists())


if __name__ == "__main__":
    unittest.main()
