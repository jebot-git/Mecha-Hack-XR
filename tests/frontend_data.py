import importlib.util,unittest,sys,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from audit_is2 import progression
spec=importlib.util.spec_from_file_location('decode_frontend',ROOT/'tools/decode_frontend.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class MissionData(unittest.TestCase):
 def test_is2_starting_weapons_and_additional_rewards(self):
  raw=bytearray(0x25d0+40);raw[0x237e:0x2384]=bytes.fromhex('31fc00154516')
  rewards=[3,7,13,6,5,1,11,12,-1,9]+[-1]*10
  struct.pack_into('>20h',raw,0x25d0,*rewards)
  result=progression(raw)
  self.assertEqual(result['initial_weapons'],(1<<1)|(1<<3)|(1<<5))
  self.assertEqual(result['mission_rewards'],[4,8,9,7,6,2,10,11,-1,12]+[-1]*10)
  self.assertEqual([(r['mission'],r['original_weapon'],r['native_weapon']) for r in result['additional_rewards']],[(3,13,9),(7,11,10),(8,12,11),(10,9,12)])
  raw[0x237e]=0
  with self.assertRaises(ValueError):progression(raw)
  raw[0x237e]=0x31;struct.pack_into('>h',raw,0x25d0,14)
  with self.assertRaises(ValueError):progression(raw)
 def test_local_records(self):
  path=ROOT/'assets/retail/blocks/00.bin'
  if not path.exists():self.skipTest('local cartridge not imported')
  raw=path.read_bytes();result=m.decode(raw)['missions']
  self.assertEqual(len(result),16)
  self.assertEqual(result[0]['title'],'THE BUST')
  self.assertEqual(result[-1]['title'],'WAR IS HECK')
  self.assertTrue(all(4<=len(x['lines'])<=8 for x in result))
  self.assertTrue(result[0]['lines'][0]['text'].startswith('The Resistance'))
  corrupt=bytearray(raw);corrupt[0x8d3e:0x8d42]=b'\xff'*4
  with self.assertRaises(ValueError):m.decode(corrupt)
  corrupt=bytearray(raw);start=result[0]['source_offset'];corrupt[start+4]=128
  with self.assertRaises(ValueError):m.decode(corrupt)
if __name__=='__main__':unittest.main()
