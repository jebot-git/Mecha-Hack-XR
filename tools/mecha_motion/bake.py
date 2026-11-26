"""Retarget learned phase channels onto the metric joints of a published rigid mesh."""

import hashlib
import json
import math
from pathlib import Path

import numpy as np
from scipy.spatial.transform import Rotation

FPS = 24
SAMPLES = 121
CYCLE_SECONDS = 1.14


def smooth(x):
    x = np.clip(x, 0, 1)
    return x * x * (3 - 2 * x)


def distance(cycle):
    """Unbounded approach, then a half-cosine brake over the final full cycle."""
    u = np.clip(cycle - 1, 0, 1)
    return np.where(cycle < 1, cycle, 1 + 0.5 * u + np.sin(np.pi * u) / (2 * np.pi))


def sample(values, phases):
    a = np.asarray(values, dtype=float)
    if a.ndim != 1 or len(a) < 2 or not np.isfinite(a).all():
        raise ValueError("A finite sampled phase channel is required")
    return np.interp(phases, np.linspace(0.0, 1.0, len(a)), a)


def unit(v):
    return v / np.maximum(1e-12, np.linalg.norm(v, axis=-1, keepdims=True))


def rigid(rotation, origin, target):
    """Map a rest pivot to a frame-space target without skin or bone stretch."""
    m = np.tile(np.eye(4), (len(rotation), 1, 1))
    m[:, :3, :3] = rotation
    m[:, :3, 3] = target - rotation @ origin
    return m


def apply(m, v):
    return m[:, :3, :3] @ v + m[:, :3, 3]


def align(original, animated):
    a, b = unit(original), unit(animated)
    cosine = np.clip(b @ a, -1, 1)
    q = np.column_stack([np.cross(a, b), 1 + cosine])
    anti = cosine < -0.9999999
    if anti.any():
        pole = np.eye(3)[np.argmin(abs(a))]
        q[anti] = [*unit(np.cross(a, pole)), 0]
    return Rotation.from_quat(q).as_matrix()


def solve_leg(hip, knee, ankle, base, target):
    upper, lower = knee - hip, ankle - knee
    a, b = np.linalg.norm(upper), np.linalg.norm(lower)
    if min(a, b) < 1e-6:
        raise ValueError("Leg chain has a zero-length segment")
    reach = np.linalg.norm(target - base, axis=1)
    if np.any(reach > a + b + 1e-7) or np.any(reach < abs(a - b) - 1e-7):
        return None
    axis = unit(target - base)
    along = (reach * reach + a * a - b * b) / (2 * np.maximum(reach, 1e-12))
    rest = unit(ankle - hip)
    pole = upper - rest * (upper @ rest)
    if np.linalg.norm(pole) < a * 0.002:
        # Straight hydraulic pistons still need a stable knee plane.
        pole = np.array([0.0, 0.0, -1.0])
    bend = unit(pole - axis * (axis @ pole)[:, None])
    mid = base + along[:, None] * axis + np.sqrt(np.maximum(0, a * a - along * along))[:, None] * bend
    return rigid(align(upper, mid - base), hip, base), rigid(align(lower, target - mid), knee, mid)


def triangles_by_joint(model, rig):
    vertices = np.asarray(model["vertices"]) * rig["mesh_scale"]
    expanded = []
    for face in model["faces"]:
        ids = face["indices"]
        for j in range(1, len(ids) - 1):
            expanded.extend((ids[0], ids[j], ids[j + 1]))
    if len(expanded) != rig["mesh_vertices"]:
        raise ValueError("Mesh draw count differs from the published rig")
    stream = np.array(expanded)
    used = np.zeros(len(stream), dtype=bool)
    result = []
    for part in rig["parts"]:
        chunks = []
        for start, count in part["ranges"]:
            if (
                start < 0
                or count < 0
                or start % 3
                or count % 3
                or start + count > len(stream)
                or used[start : start + count].any()
            ):
                raise ValueError("Invalid or overlapping draw ranges for " + part["name"])
            chunks.append(stream[start : start + count])
            used[start : start + count] = True
        ids = np.unique(np.concatenate(chunks)) if chunks else []
        result.append(vertices[ids])
    if not used.all():
        raise ValueError("Published rig leaves unassigned geometry")
    return result


def solve(rig, curves, geometry, amount):
    parts = rig["parts"]
    legs = rig["legs"]
    piv = np.array([p["pivot"] for p in parts])
    time = np.arange(SAMPLES) / FPS
    cycle = time / CYCLE_SECONDS
    swing_weight = 1 - smooth(cycle - 1)
    # Wait until the trailing foot settles before starting the brace.
    brace_phase = np.minimum((time - 2.9) / 0.68, 1 - (time - 4.15) / 0.85)
    depth = sample(curves["depth"], np.clip(brace_phase, 0, 1))
    shoulder = sample(curves["shoulder"], np.clip(brace_phase, 0, 1))
    sway = sample(curves["sway"], cycle % 1.0)
    if legs:
        height = min(piv[l["hip"], 1] - piv[l["foot"], 1] for l in legs)
        if height <= 0:
            raise ValueError("Hip must sit above its foot bind pivot")
    else:
        body = np.concatenate([v for v in geometry if len(v)])
        height = float(np.ptp(body[:, 1]))
    stride = height * 0.99 * amount
    travel = stride * distance(cycle)
    root = np.column_stack([np.zeros_like(time), np.zeros_like(time), -travel])
    bite = rig["family"] == "kaiju" and any(p["role"] == "jaw" for p in parts)

    def pulse(t, start, peak, end):
        return smooth((t - start) / (peak - start)) * (1 - smooth((t - peak) / (end - peak)))

    coil = depth * (1 - smooth((time - 3.69) / 0.24)) if bite else depth
    strike = pulse(time, 3.69, 3.94, 4.47) if bite else np.zeros_like(time)
    recovery = pulse(time, 4.09, 4.41, 4.94) if bite else np.zeros_like(time)
    body_pose = np.tile(np.eye(4), (SAMPLES, 1, 1))
    body_pose[:, 0, 3] = -0.047 * height * amount * sway * swing_weight
    body_pose[:, 1, 3] = (
        (-0.067 - 0.021 * np.cos(cycle * 4 * np.pi) * swing_weight - 0.13 * coil - 0.067 * strike)
        * height
        * amount
    )
    if bite:
        body_pose[:, 2, 3] = height * amount * (0.085 * coil - 0.135 * strike + 0.025 * recovery)
    contact = {}
    overrides = {}
    for leg in legs:
        h, k, f = (leg[x] for x in ("hip", "knee", "foot"))
        shift = (leg["phase"] / (2 * np.pi)) % 1.0
        local = cycle - shift
        phase = local % 1.0
        start = np.floor(local) + shift
        side = "left" if leg["side"] < 0 else "right"
        duty = 0.54 if side == "left" else 0.52
        swing = np.clip((phase - duty) / (1 - duty), 0, 1)
        anchor = distance(start + duty / 2) * stride
        span = distance(start + 1 + duty / 2) * stride - anchor
        lift = sample(curves[side], swing) * np.minimum(1.0, span / max(stride * 0.48, 1e-6))
        lift[(swing == 0) | (swing == 1)] = 0.0
        target = np.tile(piv[f], (SAMPLES, 1))
        target[:, 1] += rig["floor"] - geometry[f][:, 1].min() + height * 0.17 * amount * lift
        target[:, 2] += travel - anchor - smooth(swing / 0.80) * span
        answer = solve_leg(piv[h], piv[k], piv[f], apply(body_pose, piv[h]), target)
        if answer is None:
            return None
        overrides[h], overrides[k] = answer
        overrides[f] = np.tile(np.eye(4), (SAMPLES, 1, 1))
        overrides[f][:, :3, 3] = target - piv[f]
        contact[f] = (phase <= duty) | (span < 1e-7)

    frames = np.empty((SAMPLES, len(parts), 4, 4))
    tail_depth = {}
    for i, p in enumerate(parts):
        if i in overrides:
            frames[:, i] = overrides[i]
            continue
        base = body_pose if p["parent"] < 0 else frames[:, p["parent"]]
        role = p["role"]
        side = p.get("side") or (1 if piv[i, 0] >= 0 else -1)
        pitch, yaw, roll = np.zeros((3, SAMPLES))
        beat = np.sin(2 * np.pi * cycle)
        if role == "torso":
            pitch = 0.035 * swing_weight + 0.15 * coil - 0.18 * strike + 0.055 * recovery
            yaw = 0.085 * beat * swing_weight
        elif role == "head":
            pitch = -0.09 * coil + 0.21 * strike
            yaw = -0.04 * beat * swing_weight
        elif p.get("hinge") == "elbow":
            pitch = 0.13 + 0.14 * swing_weight * (1 + side * beat) + 0.66 * shoulder
        elif role == "arm" or p.get("tracked_segment") in {"upper-arm", "forearm"}:
            pitch = -side * 0.19 * beat * swing_weight + shoulder * (
                0.47 if p.get("tracked_segment") == "forearm" else 0.22
            )
        elif role == "wing":
            roll = side * (0.095 * np.sin(2 * np.pi * cycle) * swing_weight + 0.13 * depth)
        elif role == "tail":
            phase = p.get("phase", 0.0)
            yaw = 0.13 * np.sin(cycle * 2 * np.pi - phase) * swing_weight + 0.065 * depth * math.cos(phase)
            pitch = 0.025 * np.cos(cycle * 2 * np.pi - phase) * swing_weight
            if bite:
                segment = tail_depth.get(p["parent"], -1) + 1
                tail_depth[i] = segment
                delayed = time - min(0.055 * segment, 0.23)
                curl = pulse(delayed, 2.92, 3.68, 4.13)
                lash = pulse(delayed, 3.73, 3.99, 4.52)
                settle = pulse(delayed, 4.16, 4.51, 4.99)
                yaw += 0.21 * curl - 0.29 * lash + 0.065 * settle
                pitch += 0.074 * curl - 0.11 * lash
        elif role == "jaw":
            opening = 0.36 * coil + 0.62 * pulse(time, 3.70, 3.86, 4.10) if bite else 0.29 * shoulder
            axis = p.get("rotation_axis")
            if axis == "y":
                yaw = side * opening
            elif axis == "z":
                roll = side * opening
            else:
                pitch = -opening
        elif role == "suspension":
            roll = 0.035 * side * depth
        angles = np.column_stack([pitch, yaw, roll]) * amount
        if np.any(angles):
            spin = Rotation.from_euler("xyz", angles).as_matrix()
            frames[:, i] = base @ rigid(spin, piv[i], piv[i])
        else:
            frames[:, i] = base
    if legs:
        # Hanging gear can reach below the hips during a deep guard. Retry the
        # action at a smaller amplitude rather than lifting the planted feet.
        for i, v in enumerate(geometry):
            if (
                len(v)
                and np.min(frames[:, i, 1, :3] @ v.T + frames[:, i, 1, 3, None]) < rig["floor"] - 0.0001
            ):
                return None
    else:
        # Ground wurms undulate along their segmented body. The aquatic variant
        # keeps the same bed clearance while its paddles and tentacles articulate.
        bottom = np.full(SAMPLES, np.inf)
        for i, v in enumerate(geometry):
            if len(v):
                ys = frames[:, i, 1, :3] @ v.T + frames[:, i, 1, 3, None]
                bottom = np.minimum(bottom, ys.min(axis=1))
        clearance = height * 0.14 if any(p["role"] == "wing" for p in parts) else 0.0
        frames[:, :, 1, 3] += (rig["floor"] + clearance - bottom)[:, None]
    return frames, root, contact


def bake(pack, rig_id, rig_record, curves, output_path):
    pack, output_path = Path(pack), Path(output_path)
    rig = rig_record
    with (pack / rig["mesh"]).open() as file:
        geometry = triangles_by_joint(json.load(file), rig)
    # A compact, tightly folded chain may have less room for travel or
    # compression. Reduce travel for the whole actor, keeping all feet in sync.
    for scale in (1.0, 0.8, 0.6, 0.4, 0.25, 0.125, 0.0625):
        result = solve(rig, curves, geometry, scale)
        if result is not None:
            break
    else:
        raise ValueError(f"{rig_id}: no reachable contact trajectory")
    frames, root, contacts = result
    q = Rotation.from_matrix(frames[:, :, :3, :3].reshape(-1, 3, 3)).as_quat().reshape(*frames.shape[:2], 4)
    for i in range(1, SAMPLES):
        q[i, np.sum(q[i] * q[i - 1], axis=1) < 0] *= -1
    values = np.round(np.concatenate([q, frames[:, :, :3, 3]], axis=2), 7)
    roots = root.round(7)
    if not np.isfinite(values).all() or not np.isfinite(roots).all():
        raise ValueError(f"{rig_id}: nonfinite baked transform")
    # Evaluate the encoded transforms, including rounding, just as an importer does.
    r = Rotation.from_quat(values[:, :, :4].reshape(-1, 4)).as_matrix().reshape(*frames.shape[:2], 3, 3)
    bottom = np.full(SAMPLES, np.inf)
    max_slip = max_gap = 0.0
    samples = 0
    for i, v in enumerate(geometry):
        if not len(v):
            continue
        ys = r[:, i, 1, :] @ v.T + values[:, i, 5, None] + roots[:, 1, None]
        bottom = np.minimum(bottom, ys.min(axis=1))
        if i in contacts:
            mask = contacts[i]
            sole = v[v[:, 1] <= v[:, 1].min() + 1e-5].mean(axis=0)
            point = r[:, i] @ sole + values[:, i, 4:] + roots
            hold = mask[1:] & mask[:-1]
            samples += int(hold.sum())
            if np.any(hold):
                speed = np.linalg.norm(np.diff(point[:, [0, 2]], axis=0), axis=1) * FPS
                max_slip = max(max_slip, float(speed[hold].max()))
            max_gap = max(max_gap, float(np.max(abs(point[mask, 1] - rig["floor"]))))
    penetration = max(0.0, float(rig["floor"] - bottom.min()))
    if max_slip > 0.002 or max_gap > 0.002 or penetration > 0.0002:
        raise ValueError(f"{rig_id}: encoded motion violates contact or floor clearance")
    metrics = {
        "legs": len(contacts),
        "stance_intervals": samples,
        "max_contact_speed_mps": round(max_slip, 6) if contacts else None,
        "max_contact_height_error_m": round(max_gap, 6) if contacts else None,
        "max_mesh_penetration_m": round(penetration, 6),
        "reach_scale": scale,
        "travel_m": round(-float(root[-1, 2]), 4),
    }
    record = {k: rig[k] for k in ("mesh", "mesh_scale", "source", "family", "kind", "parts")}
    action = (
        "coil-bite-recovery"
        if rig["family"] == "kaiju" and any(p["role"] == "jaw" for p in rig["parts"])
        else "brace-recovery"
    )
    record.update(
        schema=1,
        action=action,
        rig_id=rig_id,
        fps=FPS,
        root=roots.tolist(),
        frames=values.tolist(),
        contacts=[[i for i, mask in contacts.items() if mask[n]] for n in range(SAMPLES)],
        metrics=metrics,
    )
    output_path.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(record, separators=(",", ":"), allow_nan=False).encode()
    output_path.write_bytes(payload)
    return {"metrics": metrics, "frames": SAMPLES, "sha256": hashlib.sha256(payload).hexdigest()}
