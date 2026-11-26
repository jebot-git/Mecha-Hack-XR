"""Check the non-gait action studies against every published rigid binding."""
import json
import sys
import unittest
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from mecha_motion import vehicle_actions as vehicle
from mecha_motion.bake import triangles_by_joint
from mecha_motion.rigs import discover as gait_inventory


class VehicleMotionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from tools.mecha_motion.__main__ import DEFAULT_PACK
        cls.pack = DEFAULT_PACK
        cls.rigs = vehicle.discover(cls.pack)
        reference = ROOT / "art/mecha/motion-evaluation/reference/brace.json"
        cls.curves = json.loads(reference.read_text())

    def context(self, rig):
        return {"curves": self.curves, "model": json.loads((self.pack / rig["mesh"]).read_bytes())}

    def test_repertoire_geometry(self):
        _, excluded = gait_inventory(self.pack)
        self.assertEqual(self.rigs.keys(), excluded.keys())
        t = np.linspace(0, 1, 43)
        for name, rig in self.rigs.items():
            context = self.context(rig)
            geometry = triangles_by_joint(context["model"], rig)
            names = vehicle.actions(name, rig, context)
            self.assertEqual(len(names), len(set(names)))
            for action in names:
                with self.subTest(rig=name, action=action):
                    matrices, root = vehicle.pose(name, rig, action, t, context)
                    self.assertEqual(matrices.shape, (len(t), len(geometry), 4, 4))
                    self.assertEqual(root.shape, (len(t), 3))
                    self.assertTrue(np.isfinite(matrices).all() and np.isfinite(root).all())
                    np.testing.assert_allclose(np.linalg.det(matrices[:, :, :3, :3]), 1, atol=1e-8)
                    self.assertGreater(max(np.ptp(matrices, axis=0).max(), np.ptp(root, axis=0).max()), .01)
                    for i, vertices in enumerate(geometry):
                        if len(vertices):
                            y = matrices[:, i, 1, :3] @ vertices.T
                            y += matrices[:, i, 1, 3, None] + root[:, 1, None]
                            self.assertGreaterEqual(y.min(), rig["floor"] - 1e-6)
                    if rig["kind"] == "stationary":
                        np.testing.assert_array_equal(root, np.zeros_like(root))
                        fixed = [i for i, p in enumerate(rig["parts"]) if p["role"] == "rigid"]
                        np.testing.assert_allclose(matrices[:, fixed], np.tile(np.eye(4), (len(t), len(fixed), 1, 1)))
                    self.assertGreater(vehicle.duration(action), 0)

    def test_six_guns_converge_on_one_moving_target(self):
        rig = self.rigs["dreadnought"]
        context = self.context(rig)
        t = np.linspace(0, 1, 101)
        mat, _ = vehicle.pose("dreadnought", rig, "aim-track", t, context)
        length = np.ptp(np.asarray(context["model"]["vertices"]), axis=0)[2]
        azimuth = .78 * np.sin(2 * np.pi * t)
        elevation = .26 * (1 - np.cos(2 * np.pi * t))
        local_target = np.asarray(rig["parts"][0]["pivot"]) + 2.5 * length * np.column_stack(
            (-np.sin(azimuth), np.tan(elevation), -np.cos(azimuth)))
        hull = mat[:, 0]
        target = np.einsum("nij,nj->ni", hull[:, :3, :3], local_target) + hull[:, :3, 3]
        for i, part in enumerate(rig["parts"]):
            if part["role"] != "barrel":
                continue
            pivot = (mat[:, i] @ np.array([*part["pivot"], 1]))[:, :3]
            desired = target - pivot
            desired /= np.linalg.norm(desired, axis=1, keepdims=True)
            np.testing.assert_allclose(desired, -mat[:, i, :3, 2], atol=1e-5)

    def test_correct_weapon_actuates_on_release(self):
        t = np.linspace(0, 1, 121)
        for name, action, fires_barrel in (
            ("gunboat", "missile-salvo", False), ("carrier", "launch-air-wing", False),
            ("bomber", "bombing-run", False), ("artillery_battery", "artillery-bombardment", True),
        ):
            with self.subTest(rig=name, action=action):
                rig = self.rigs[name]
                mat, _ = vehicle.pose(name, rig, action, t, self.context(rig))
                for i, part in enumerate(rig["parts"]):
                    if part["role"] != "barrel":
                        continue
                    parent_local = np.linalg.inv(mat[:, part["parent"]]) @ mat[:, i]
                    pivot = np.array([*part["pivot"], 1])
                    slide = (parent_local @ pivot)[:, :3] - pivot[:3]
                    if fires_barrel:
                        self.assertGreater(np.linalg.norm(slide, axis=1).max(), .1)
                        # Every displacement is along the aimed bore, even during elevation.
                        np.testing.assert_allclose(np.cross(parent_local[:, :3, 2], slide), 0, atol=1e-7)
                    else:
                        np.testing.assert_allclose(slide, 0, atol=1e-7)

    def test_flight_contact_recovery_and_scrubbing(self):
        t = np.linspace(0, 1, 101)
        rig = self.rigs["interceptor"]
        track, root = vehicle.pose("interceptor", rig, "banked-pass", t, self.context(rig))
        course = np.arctan2(np.gradient(root[:, 0]), -np.gradient(root[:, 2]))
        heading = np.arctan2(-track[:, 0, 0, 2], track[:, 0, 2, 2])
        self.assertLess(np.rad2deg(np.mean((course - heading) ** 2)**.5), 2.0)
        rig = self.rigs["bomber"]
        _, root = vehicle.pose("bomber", rig, "bombing-run", t, self.context(rig))
        self.assertGreater(root[0, 1] - root[50, 1], 1.)
        self.assertGreater(root[-1, 1] - root[50, 1], 1.)
        rig = self.rigs["salvager_gunship"]
        context = self.context(rig)
        track, root = vehicle.pose("salvager_gunship", rig, "destruction", t, context)
        np.testing.assert_allclose(track[-1], track[-6], atol=1e-7)
        np.testing.assert_allclose(root[-1, :2], root[-6, :2], atol=1e-7)
        # Spin has stopped while the wreck finishes its short horizontal skid.
        self.assertLess(abs(root[-1, 2] - root[-6, 2]), .01 * np.ptp(root[:, 2]))
        indices = [73, 20, 50, 99, 9]
        sampled, position = vehicle.pose("salvager_gunship", rig, "destruction", t[indices], context)
        np.testing.assert_allclose(sampled, track[indices])
        np.testing.assert_allclose(position, root[indices])

    def test_capability_and_broadside(self):
        def actions(name):
            return vehicle.actions(name, self.rigs[name], None)
        self.assertIn("pivot-turn", actions("anti_mecha_hovertank"))
        self.assertNotIn("pivot-turn", actions("heavy_hover_tank"))
        self.assertIn("sonar-pulse", actions("deepdiver_submarine"))
        self.assertIn("bombing-run", actions("bomber"))
        self.assertNotIn("bombing-run", actions("interceptor"))
        self.assertEqual(actions("ally"), ["impact-stagger", "destruction"])
        rig = self.rigs["dreadnought"]
        t = np.linspace(0, 1, 81)
        mat, _ = vehicle.pose("dreadnought", rig, "heavy-broadside", t, self.context(rig))
        # Measure the displacement of each breech along its own parent carriage.
        # The six guns should release from forward to aft in a visible sequence.
        peaks = []
        for i, p in enumerate(rig["parts"]):
            if p["role"] == "barrel":
                local = np.linalg.inv(mat[:, p["parent"]]) @ mat[:, i]
                pivot = np.asarray([*p["pivot"], 1])
                travel = (local @ pivot)[:, 2] - pivot[2]
                self.assertGreater(travel.max(), .1)
                peaks.append(int(np.argmax(travel)))
        self.assertGreaterEqual(len(peaks), 3)
        self.assertTrue(all(b > a for a, b in zip(peaks, peaks[1:])))


if __name__ == "__main__":
    unittest.main()
