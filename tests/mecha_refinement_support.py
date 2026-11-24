"""Resolve explicitly revised sources to their immutable historical snapshots."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[1]
PAIRS=('jump_pod','jump_pod_heavy','jump_pod_field_commander','mechwalker','mechwalker_disabler','mecha_ace','siege_mecha')
IDS={n+s for n in PAIRS for s in ('','_alt')}|{'heavy_hover_tank','cyclone_drone'}
def revision():
 path=ROOT/'assets/mecha/enemy-reference-refinement.json'
 if not path.exists():return None
 data=json.loads(path.read_text());assert data['schema']==1 and data['all_pass'] is True
 assert len(data['assets'])==16 and {r['id']for r in data['assets']}==IDS
 return data
def historical_source(path):
 path=Path(path);data=revision()
 if data:
  for row in data['assets']:
   for field,old in [('file','previous_native'),('model','previous_model')]:
    if path.resolve()==(ROOT/'assets/mecha'/row[field]).resolve():return ROOT/'assets/mecha'/row[old]
 return path
def current_rigs(rigs):
 data=revision()
 if data:
  updates=json.loads((ROOT/'assets/mecha'/data['rig_file']).read_text())['rigs'];assert set(updates)==IDS
  return {k:updates.get(k,v)for k,v in rigs.items()}
 return rigs
