"""Extract timing and clearance from the published G1 waypoint example."""
import hashlib
import json
import sys
from pathlib import Path

import numpy as np


def events(active, rising):
    before, after = active[:-1], active[1:]
    return np.flatnonzero((~before & after) if rising else (before & ~after)) + 1


def swing_profile(height, off, on):
    # Clearance over the line joining the two support samples.
    points = np.linspace(off - 1, on, 9)
    heights = np.interp(points, np.arange(len(height)), height)
    clearance = np.maximum(heights - np.linspace(heights[0], heights[-1], 9), 0)
    return np.round(clearance / clearance.max(), 3).tolist()


def measure(path):
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != 'eda98d489984ac45289336207dafe3215757e7bc75b770a6a96239e093b4a0a9':
        raise ValueError('Expected the G1 root-waypoints clip listed in methods.md')
    with np.load(path, allow_pickle=False) as data:
        flags, joints = data['foot_contacts'], data['posed_joints']
    contacts = [flags[:, :2].any(1), flags[:, 2:].any(1)]
    sides = {}
    start, end = 99, 134
    for side, active, ankle in zip(('left', 'right'), contacts, (6, 13)):
        lift = next(int(i) for i in events(active, False) if start <= i < end)
        plant = next(int(i) for i in events(active, True) if i > lift)
        sides[side] = dict(
            median_stride_frames=float(np.median(np.diff(events(active, True)))),
            contact_fraction=round(float(active.mean()), 4),
            swing_frames=[lift, plant],
            swing_clearance=swing_profile(joints[:, ankle, 1], lift, plant),
        )
    return dict(source_sha256=digest, frames=len(flags), fps=30, cycle_frames=[start, end],
                double_support_fraction=round(float(np.logical_and(*contacts).mean()), 4),
                sides=sides)


if __name__ == '__main__':
    print(json.dumps(measure(Path(sys.argv[1])), indent=2))
