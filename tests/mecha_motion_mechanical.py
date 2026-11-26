"""Mechanical expansion checks against the real baked rigid meshes and catalog."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from mecha_motion.bake import apply, bake
from mecha_motion.mechanical_actions import actions, duration, pose, timing_anchors
from mecha_motion.rigs import discover


class MechanicalActionsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from tools.mecha_motion.__main__ import DEFAULT_PACK
        cls.pack = DEFAULT_PACK
        cls.rigs, _ = discover(cls.pack)
        refs = ROOT / "art/mecha/motion-evaluation"
        gait = json.loads((refs / "kimodo-gait.json").read_text())["sides"]
        brace = json.loads((refs / "reference/brace.json").read_text())
        cls.curves = {side: gait[side]["swing_clearance"] for side in ("left", "right")}
        cls.curves.update(shoulder=brace["shoulder"], depth=brace["depth"],
                          sway=json.loads((refs / "reference/kimodo-sway.json").read_text())["lateral"])

    def context(self, name):
        rig = self.rigs[name]
        with tempfile.TemporaryDirectory() as scratch:
            clip = Path(scratch) / "test.json"
            bake(self.pack, name, rig, self.curves, clip)
            reference = json.loads(clip.read_text())
        return {"reference": reference, "pack": self.pack, "curves": self.curves,
                "model": json.loads((self.pack / rig["mesh"]).read_text())}

    def test_action_selection(self):
        def assigned(name):
            return actions(name, self.rigs[name], {"pack": self.pack})
        self.assertIn("parry", assigned("mecha_ace"))
        self.assertIn("parry", assigned("mecha_sniper"))  # Its catalog weapon is a bayonet.
        self.assertNotIn("parry", assigned("anti_mecha_strike_team"))
        self.assertNotIn("skirt-volley", assigned("mecha_sniper"))
        self.assertIn("grenade-throw", assigned("anti_mecha_strike_team"))
        self.assertNotIn("boost-evade", assigned("anti_mecha_strike_team"))
        self.assertIn("pilot-ejection", assigned("chassis_brawler"))

    def test_planted_stride_and_rigid_loadout(self):
        name = "mecha_ace"
        rig, ctx = self.rigs[name], self.context(name)
        t = np.linspace(0, 1, 97)
        joints = {p["name"]: i for i, p in enumerate(rig["parts"])}
        for action in ("walk", "retreat", "strafe", "pivot-turn"):
            with self.subTest(action=action):
                mx, root = pose(name, rig, action, t, ctx)
                for leg in rig["legs"]:
                    foot = leg["foot"]
                    pos = apply(mx[:,foot], rig["parts"][foot]["pivot"]) + root
                    phase = (2*t - leg["phase"]/(2*np.pi)) % 1
                    duty = .54 if leg["side"] < 0 else .52
                    stance = (phase[:-1] < duty) & (phase[1:] < duty) & (np.diff(phase) >= 0)
                    self.assertLess(abs(np.diff(pos[:,[0,2]], axis=0)[stance]).max(), 1e-5)
                np.testing.assert_allclose(np.linalg.det(mx[:,:,:3,:3]), 1, atol=3e-6)
        for action in ("parry", "fire-recoil", "overhead-cut"):
            mx, _ = pose(name, rig, action, t, ctx)
            np.testing.assert_allclose(mx[:,joints["barrel_1"]], mx[:,joints["forearm_1"]], atol=1e-6)
            relative = np.linalg.inv(mx[:,joints["arm_1"]]) @ mx[:,joints["forearm_1"]]
            self.assertGreater(np.ptp(relative[:,:3,:3], axis=0).max(), .15)

    def test_leap_load_flight_and_ground_contact(self):
        name = "jump_pod"
        rig, ctx = self.rigs[name], self.context(name)
        t = np.array([0, .16, .26, .495, .73, .81, 1])
        frames, root = pose(name, rig, "leap-land", t, ctx)
        piv = np.array([p["pivot"] for p in rig["parts"]])
        hip, foot = (rig["legs"][0][k] for k in ("hip", "foot"))
        height = piv[hip,1] - piv[foot,1]
        hip_y = apply(frames[:,hip], piv[hip])[:,1]
        self.assertGreater(hip_y[0] - hip_y[1], .19*height)
        self.assertGreater(hip_y[4] - hip_y[5], .20*height)
        np.testing.assert_allclose(root[[0,1,2,4,5,6],1], 0, atol=1e-7)
        self.assertGreater(root[3,1], .85*height)
        np.testing.assert_allclose(root[4:], np.tile(root[4], (3,1)))
        sole = apply(frames[:,foot], piv[foot])
        np.testing.assert_allclose(sole[[1,2,4,5,6]], np.tile(sole[0], (5,1)), atol=1e-5)
        self.assertEqual(timing_anchors("leap-land"), (.26,.73))

    def test_ranged_elbow_and_delayed_weight_transfer(self):
        name = "mecha_sniper"
        rig, ctx = self.rigs[name], self.context(name)
        parts = {p["name"]: i for i,p in enumerate(rig["parts"])}
        t = np.array([0,.14,.22,.30,.50,1.])
        frames, root = pose(name, rig, "fire-recoil", t, ctx)
        parent, elbow = parts["right_upper_arm"], parts["right_forearm"]
        relative = np.linalg.solve(frames[:,parent], frames[:,elbow])
        self.assertGreater(np.ptp(relative[:,:3,:3], axis=0).max(), .35)
        # The rifle is integrated into the left forearm on this specific mesh.
        muzzle = -frames[:,parts["left_forearm"],:3,2]
        self.assertLess(np.abs(np.rad2deg(np.arcsin(muzzle[:,1]))).max(), 15)
        hip = rig["legs"][0]["hip"]
        pelvis = apply(frames[:,hip], rig["parts"][hip]["pivot"])
        self.assertGreater(pelvis[3,2], pelvis[2,2])
        np.testing.assert_allclose(root, 0)
        for leg in rig["legs"]:
            foot = leg["foot"]
            sole = apply(frames[:,foot], rig["parts"][foot]["pivot"])
            np.testing.assert_allclose(sole, np.broadcast_to(sole[0], sole.shape), atol=1e-5)
        self.assertEqual(len(timing_anchors("skirt-volley")), 3)
        np.testing.assert_allclose(np.diff(timing_anchors("lofted-missile-salvo"))*
                                   duration("lofted-missile-salvo"), .26)

    def test_squad_falls_by_individual(self):
        name = "anti_mecha_strike_team"
        rig, ctx = self.rigs[name], self.context(name)
        mx, _ = pose(name, rig, "collapse", np.linspace(0, 1, 31), ctx)
        torsos = [i for i,p in enumerate(rig["parts"]) if p["role"] == "torso"]
        self.assertGreater(len(torsos), 1)
        self.assertLess(mx[-1,torsos[0],1,0] * mx[-1,torsos[1],1,0], -.5)
        self.assertGreater(duration("collapse"), duration("stagger"))

if __name__ == "__main__":
    unittest.main()
