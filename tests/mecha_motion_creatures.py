"""Regression checks for the offline creature action catalogue and planted-claw rigging."""

import json
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from mecha_motion import creature_actions as creatures
from mecha_motion.bake import apply, bake, triangles_by_joint
from mecha_motion.rigs import discover


class CreatureActionsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from tools.mecha_motion.__main__ import DEFAULT_PACK
        cls.pack = DEFAULT_PACK
        cls.rigs, _ = discover(cls.pack)
        cls.rigs = {k: r for k, r in cls.rigs.items() if r["family"] == "kaiju"}
        ref = ROOT / "art/mecha/motion-evaluation"
        gait = json.loads((ref / "kimodo-gait.json").read_bytes())["sides"]
        brace = json.loads((ref / "reference/brace.json").read_bytes())
        cls.curves = {side: gait[side]["swing_clearance"] for side in ("left", "right")}
        cls.curves.update(
            depth=brace["depth"],
            shoulder=brace["shoulder"],
            sway=json.loads((ref / "reference/kimodo-sway.json").read_bytes())["lateral"],
        )
        cls.contexts = {}
        with tempfile.TemporaryDirectory() as tmp:
            for key, rig in cls.rigs.items():
                result = bake(cls.pack, key, rig, cls.curves, Path(tmp) / "clip.json")
                cls.contexts[key] = {
                    "model": json.loads((cls.pack / rig["mesh"]).read_bytes()),
                    "pack": cls.pack,
                    "reference": result,
                    "curves": cls.curves,
                }

    def test_catalogue_geometry_and_seams(self):
        self.assertEqual(len(self.rigs), 20)
        t = np.linspace(0.0, 1.0, 41)
        for key, rig in self.rigs.items():
            context = self.contexts[key]
            geometry = triangles_by_joint(context["model"], rig)
            repertoire = creatures.actions(key, rig, context)
            self.assertEqual(len(repertoire), len(set(repertoire)))
            self.assertTrue({"death", "stagger", "idle", "warp-arrival"} <= set(repertoire))
            for action in repertoire:
                with self.subTest(key=key, action=action):
                    self.assertGreater(creatures.duration(action), 0)
                    matrices, root = creatures.pose(key, rig, action, t, context)
                    self.assertEqual(matrices.shape, (len(t), len(rig["parts"]), 4, 4))
                    self.assertTrue(np.isfinite(matrices).all())
                    self.assertEqual(root.shape, (len(t), 3))
                    for j, part in enumerate(rig["parts"]):
                        parent = part["parent"]
                        if parent >= 0:
                            pivot = np.asarray(part["pivot"])
                            np.testing.assert_allclose(
                                apply(matrices[:, j], pivot), apply(matrices[:, parent], pivot), atol=1e-6
                            )
                    if action in creatures.LOCOMOTION or action in {"idle", "cockpit-cling"}:
                        np.testing.assert_allclose(matrices[0], matrices[-1], atol=1e-6)
                    bottom = min(
                        (matrices[:, j, 1, :3] @ v.T + matrices[:, j, 1, 3, None] + root[:, 1, None]).min()
                        for j, v in enumerate(geometry)
                        if len(v)
                    )
                    if action in ("burrow-entry", "burrow-eruption"):
                        self.assertLess(bottom, rig["floor"] - 0.5)
                    else:
                        self.assertGreaterEqual(bottom, rig["floor"] - 0.00011)

    def test_planted_feet_stay_on_each_surface(self):
        key = "kaiju_spawnling"
        rig = self.rigs[key]
        t = np.linspace(0.0, 1.0, 81)
        for action in ("walk", "retreat", "wall-crawl", "ceiling-crawl"):
            matrices, root = creatures.pose(key, rig, action, t, self.contexts[key])
            for leg in rig["legs"]:
                foot = leg["foot"]
                world = apply(matrices[:, foot], np.array(rig["parts"][foot]["pivot"])) + root
                phase = (t - leg["phase"] / (2 * np.pi)) % 1
                duty = 0.54 if leg["side"] < 0 else 0.52
                planted = phase < duty
                pairs = planted[1:] & planted[:-1]
                self.assertGreater(np.count_nonzero(pairs), 35)
                self.assertLess(np.max(np.abs(np.diff(world, axis=0)[pairs])), 1e-7)
            self.assertGreater(np.linalg.norm(root[0] - root[-1]), 0.1)

    def test_foreclaw_slashes_with_other_feet_planted(self):
        key = "kaiju_spawnling"
        rig = self.rigs[key]
        poses, root = creatures.pose(key, rig, "rending-claws", np.linspace(0.0, 1.0, 55), self.contexts[key])
        traces = [
            apply(poses[:, leg["foot"]], np.asarray(rig["parts"][leg["foot"]]["pivot"])) + root
            for leg in rig["legs"]
        ]
        self.assertEqual(sum(np.ptp(points[:, 1]) > 0.07 for points in traces), 1)
        self.assertEqual(sum(np.max(np.ptp(points, axis=0)) < 1e-7 for points in traces), len(traces) - 1)

    def test_takeoff_and_burrow_crossings(self):
        t = np.array([0, 0.10, 0.38, 0.47, 0.64, 0.83, 1])
        for key in ("kaiju_flea", "kaiju_spawnling", "kaiju_tunnel_wurm"):
            rig, context = self.rigs[key], self.contexts[key]
            geometry = triangles_by_joint(context["model"], rig)
            height = np.ptp(np.concatenate([v for v in geometry if len(v)])[:, 1])
            for action in ("tackle", "burrow-entry", "burrow-eruption"):
                with self.subTest(key=key, action=action):
                    poses, root = creatures.pose(key, rig, action, t, context)
                    y = np.concatenate([
                        poses[:, j, 1, :3] @ vertices.T + poses[:, j, 1, 3, None] + root[:, 1, None]
                        for j, vertices in enumerate(geometry) if len(vertices)
                    ], axis=1)
                    bottom = (y.min(axis=1) - rig["floor"]) / height
                    pitch = -poses[2, 0, 1, 2]  # forward axis (-Z) expressed in world Y
                    if action == "tackle":
                        np.testing.assert_allclose(bottom[[0, 1, 5, 6]], 0, atol=1e-6)
                        self.assertGreater(bottom[3], 0.56)
                    elif action == "burrow-entry":
                        self.assertLess(pitch, -0.50)
                        self.assertLess(y[-1].max(), rig["floor"])
                    else:
                        self.assertGreater(pitch, 0.50)
                        self.assertLess(y[0].max(), rig["floor"])
                        self.assertGreater(bottom[4], 0.23)
                        self.assertAlmostEqual(bottom[-1], 0)

    def test_cling_supports_while_jaws_move(self):
        t = np.linspace(0.0, 1.0, 121)
        for key, rig in self.rigs.items():
            context = self.contexts[key]
            if not rig["legs"] or "cockpit-cling" not in creatures.actions(key, rig, context):
                continue
            poses, root = creatures.pose(key, rig, "cockpit-cling", t, context)
            for leg in rig["legs"]:
                j = leg["foot"]
                points = apply(poses[:, j], np.asarray(rig["parts"][j]["pivot"])) + root
                self.assertLess(np.max(np.ptp(points, axis=0)), 1e-7, key)
            jaw = next(j for j, part in enumerate(rig["parts"]) if part["role"] == "jaw")
            parent = rig["parts"][jaw]["parent"]
            local = np.linalg.inv(poses[:, parent]) @ poses[:, jaw]
            self.assertGreater(np.max(np.ptp(local[:, :3, :3], axis=0)), 0.11, key)

    def test_siege_timing_and_dedicated_elbow(self):
        for action, (charge, discharge, _) in creatures.SIEGE.items():
            anchors = np.array(creatures.timing_anchors(action)) * creatures.duration(action)
            self.assertAlmostEqual(anchors[0], charge)
            self.assertAlmostEqual(anchors[-1], charge + discharge)
            self.assertTrue(np.all(np.diff(anchors) > 0))
        key, action = "abomination", "rocket-elbow"
        rig = self.rigs[key]
        shot = creatures.timing_anchors(action)[0]
        poses, _ = creatures.pose(key, rig, action, np.array([0, shot]), self.contexts[key])
        arms = {p["side"]: j for j, p in enumerate(rig["parts"]) if p["role"] == "arm"}
        def travel(j):
            local = np.linalg.inv(poses[:, rig["parts"][j]["parent"]]) @ poses[:, j]
            return np.linalg.norm(local[1, :3, :3] - local[0, :3, :3])
        self.assertGreater(travel(arms[1]), 2.7 * travel(arms[-1]))

    def test_partial_sampling_matches_full_clip(self):
        for key, action in (("kaiju_spawnling_alt", "recover"), ("kaiju_stalker", "grapple")):
            t = np.linspace(0.0, 1.0, 121)
            rig = self.rigs[key]
            poses, root = creatures.pose(key, rig, action, t, self.contexts[key])
            one, one_root = creatures.pose(key, rig, action, t[25:26], self.contexts[key])
            np.testing.assert_allclose(one[0], poses[25], atol=1e-7)
            np.testing.assert_allclose(one_root[0], root[25], atol=1e-7)


if __name__ == "__main__":
    unittest.main()
