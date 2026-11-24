import sys,unittest,struct,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'tools'))
from decode_models import mesh,segments
class Models(unittest.TestCase):
 def test_triangle(self):
  # Header, three signed vertices, one normal, three groups with one valid face.
  b=struct.pack('>4H',3,1,0,0)+b''.join(struct.pack('>4h',*v,0) for v in [(0,0,0),(20,0,0),(0,20,0)])+bytes(8)
  b+=struct.pack('>5H3H2H',1,3,0x8800,0,0,0,16,32,0,0)
  m=mesh(b,0);self.assertEqual(m['faces'][0]['indices'],[0,1,2]);self.assertEqual(m['source_length'],len(b))
  corrupt=bytearray(b);struct.pack_into('>H',corrupt,52,48)
  with self.assertRaises(ValueError):mesh(corrupt,0)
  with self.assertRaises(ValueError):mesh(b[:-1],0)
 def test_effect_and_pickup_provenance(self):
  root=ROOT/'assets/retail';p=root/'models/manifest.json'
  if not p.exists():self.skipTest('local cartridge absent')
  j=json.loads(p.read_text());b=(root/'blocks/07.bin').read_bytes()
  self.assertEqual(j['pickup_types'],list(b[0x2fba:0x2fda]))
  for name,record in j['models'].items():
   if record.get('source_block')!=7:continue
   m=json.loads((root/'models'/record['file']).read_text())
   descriptor=m['draw_descriptor'];pointer=struct.unpack_from('>I',b,descriptor-0x6000+16)[0]-0x6000
   self.assertEqual(m['vertices'],mesh(b,pointer)['vertices']);self.assertTrue(m['faces'])
 def test_original(self):
  p=ROOT/'assets/retail/blocks/04.bin'
  if not p.exists():self.skipTest('local cartridge absent')
  b=p.read_bytes();body=mesh(b,0xe730)
  self.assertEqual((len(body['vertices']),len(body['faces'])),(314,183))
  self.assertEqual(body['source_offset']+body['source_length'],0x1081a)
  joints,end=segments(b,0x1081a,body);self.assertEqual(end,0x10da0);self.assertEqual(len(joints),16)
  for index in [7,8,9,11,12,13]:self.assertTrue(joints[index]['faces'])
  for i in range(9):
   entry=struct.unpack_from('>I',b,0xbea+4*i)[0]-0x6000;offset=struct.unpack_from('>I',b,entry)[0]-0x6000
   model=mesh(b,offset);self.assertTrue(model['faces'])
 def test_scenery_fragment_coverage(self):
  root=ROOT/'assets/retail';manifest=root/'models/manifest.json'
  if not manifest.exists():self.skipTest('local cartridge absent')
  models=json.loads(manifest.read_text())['models'];count=0
  for path in ['gameplay/missions.json','is2/gameplay/missions.json','is2/shared/gameplay/missions.json']:
   catalog=root/path
   if not catalog.exists():continue
   for obj in json.loads(catalog.read_text())['objects']:
    for descriptor in obj.get('destruction',{}).get('fragments',[]):
     name=f'fragment-{descriptor}'
     with self.subTest(catalog=path,object=obj['type'],model=name):
      self.assertIn(name,models,'destruction model must be exported before it can be rendered')
      self.assertTrue((root/'models'/models[name]['file']).is_file())
     count+=1
  self.assertGreater(count,0,'scenery fragment references exercised')
if __name__=='__main__':unittest.main()
