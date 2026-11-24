#!/usr/bin/env python3
"""Check original layered comic delivery without creating a graphics context."""
import hashlib
import json
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'assets/mecha/cinematics/art'
PREVIEWS = ROOT / 'art/mecha/campaign-foldouts/previews'
library = json.loads((ROOT / 'assets/mecha/stories/campaigns.json').read_text())
manifest = json.loads((ART / 'manifest.json').read_text())
assert manifest['schema'] == 1
rows = manifest['assets']
expected = {f"foldout-{arc['id']}-{panel}{suffix}"
            for arc in library['campaigns'] for panel in range(3)
            for suffix in ('', '-cutout')}
assert len(rows) == len(expected) == 48
assert {r['name'] for r in rows} == expected
hashes = set()
total = 0
for row in rows:
    name = row['name']
    assert Path(row['rgba']).name == row['rgba']
    assert (row['width'], row['height']) == (512, 384)
    raw = (ART / row['rgba']).read_bytes()
    assert len(raw) == row['width'] * row['height'] * 4
    total += len(raw)
    digest = hashlib.sha256(raw).hexdigest()
    if 'sha256' in row:
        assert digest == row['sha256'], name
    assert digest not in hashes, f'Duplicate comic illustration: {name}'
    hashes.add(digest)
    image = Image.open(PREVIEWS / (name + '.png')).convert('RGBA')
    assert image.size == (512, 384) and image.tobytes() == raw
    alpha = image.getchannel('A')
    if name.endswith('-cutout'):
        histogram = alpha.histogram()
        assert histogram[0] > 1000 and sum(histogram[128:]) > 500, name
    else:
        assert len(image.convert('RGB').getcolors(512 * 384)) > 3, name
assert total <= 48 * 1024 * 1024
source = ROOT / 'art/mecha/campaign-foldouts/campaign-foldouts.blend'
assert source.is_file() and source.stat().st_size > 1024
print(f'Validated 24 distinct comic plates, 24 transparent raised cutouts and Blender source; {total} texture bytes')
