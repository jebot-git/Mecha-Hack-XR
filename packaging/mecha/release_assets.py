"""Prepare the distributable runtime including original cockpit lettering."""
import argparse
import json
from pathlib import Path
import re
import shutil
import struct

VERTEX = struct.Struct('<10f')


def cockpit_labels(runtime):
    cockpit = runtime / 'cockpit'
    path = cockpit / 'mesh-manifest.json'
    manifest = json.loads(path.read_text())
    if manifest['source'] == 'Mecha Hack XR cockpit with original HUD lettering':
        return
    mesh = (cockpit / 'cockpit.mesh').read_bytes()
    font = (runtime / 'art/hud-font.rgba').read_bytes()
    magic, version, count = struct.unpack_from('<4sII', mesh)
    assert magic == b'ISCP' and version == 1 and len(mesh) == 12 + count * VERTEX.size
    out = bytearray()
    for obj in manifest['objects']:
        name, first, size = obj['object'], obj['first_vertex'], obj['vertex_count']
        data = mesh[12 + first * VERTEX.size:12 + (first + size) * VERTEX.size]
        obj['first_vertex'] = len(out) // VERTEX.size
        if name.startswith('Legend '):
            label = name.removeprefix('Legend ')
            vertices = list(VERTEX.iter_unpack(data))
            left, top = min(v[0] for v in vertices), max(v[1] for v in vertices)
            z, *material = vertices[0][2:]
            cells = {'EQUIPPED / AMMUNITION': .022, 'IS OVERVIEW': .027, 'RADAR': .027,
                     'TORSO': .025, 'CHASSIS': .026, 'IRON SOLDIER': .028}
            step = cells.get(label, .024) / 8
            label = {'IRON SOLDIER': 'MECHA HACK', 'IS OVERVIEW': 'OVERVIEW'}.get(label, label)
            obj['object'] = 'Legend ' + label
            for i, char in enumerate(label):
                glyph = ord(char) - 32
                for row in range(8):
                    for col in range(8):
                        index = ((glyph // 16 * 8 + row) * 128 + glyph % 16 * 8 + col) * 4
                        if font[index + 3]:
                            x, y = left + (i * 8 + col) * step, top - row * step
                            dx = step * .84
                            for u, v in [(0, 0), (dx, 0), (dx, -dx), (0, 0), (dx, -dx), (0, -dx)]:
                                out.extend(VERTEX.pack(x + u, y + v, z, *material))
        else:
            out.extend(data)
        obj['vertex_count'] = len(out) // VERTEX.size - obj['first_vertex']
    count = len(out) // VERTEX.size
    (cockpit / 'cockpit.mesh').write_bytes(struct.pack('<4sII', magic, version, count) + out)
    manifest.update(vertices=count, triangles=count // 3, source='Mecha Hack XR cockpit with original HUD lettering')
    path.write_text(json.dumps(manifest, indent=2) + '\n')
    path = cockpit / 'layout.json'
    layout = json.loads(path.read_text())
    layout.pop('source_reference', None)
    path.write_text(json.dumps(layout, indent=2) + '\n')


def prepare(source, dest):
    if not (source / 'runtime-models.json').is_file():
        raise ValueError('Publish the game runtime before preparing release assets')
    shutil.copytree(source, dest, ignore=shutil.ignore_patterns('reuse-manifest.json'))
    cockpit_labels(dest)
    for path in dest.rglob('*.json'):
        raw = path.read_text()
        if '/home/' in raw:
            cleaned = re.sub(r'/home/[^/"]+/de-xr/[^/"]+/(?:artifacts/)?', '', raw)
            json.loads(cleaned)
            path.write_text(cleaned)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    prepare(args.source, args.destination)
