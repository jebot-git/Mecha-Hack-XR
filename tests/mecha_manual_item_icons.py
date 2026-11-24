from pathlib import Path
import hashlib,json
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
PACK=ROOT/'assets/mecha';ART=PACK/'art'
models=json.loads((PACK/'manual-item-assets.json').read_text())['assets']
icons=json.loads((ART/'manual-item-icons.json').read_text())
registered={row['name']:row for row in json.loads((ART/'manifest.json').read_text())['assets']}
expected={'icon-'+row['id']+'-0' for row in models}
assert {row['name'] for row in icons['assets']}==expected and len(expected)==31
fingerprints=set()
for row in icons['assets']:
    raw=(ART/row['rgba']).read_bytes();fingerprint=hashlib.sha256(raw).hexdigest()
    assert len(raw)==96*80*4 and row['width']==96 and row['height']==80
    assert fingerprint==row['sha256']==registered[row['name']]['sha256']
    assert Image.open(ART/(row['name']+'.png')).convert('RGBA').tobytes()==raw
    assert hashlib.sha256((PACK/row['native_mesh']).read_bytes()).hexdigest()==row['native_mesh_sha256']
    source=ROOT/"art/mecha/manual-assets/ui-renders"/(Path(row["native_mesh"]).stem+".png")
    assert hashlib.sha256(source.read_bytes()).hexdigest()==row["source_render_sha256"]
    assert len(Image.frombytes('RGBA',(96,80),raw).getcolors(96*80))>4
    fingerprints.add(fingerprint)
assert len(fingerprints)==31 and icons['runtime_texture_bytes']<1024*1024
print('31 distinct Blender-rendered equipment icons match native models and renderer registrations')
