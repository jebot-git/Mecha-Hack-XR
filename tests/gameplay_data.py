import sys,unittest,tempfile,json,struct,math,wave
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from decode_gameplay import unpack_sample
from decode_audio import effect_pcm
class GameplayData(unittest.TestCase):
    def test_effect_sample_formats(self):
        raw=bytes([0,1,127,128,129,255])
        self.assertEqual(effect_pcm(5,raw),[0,256,32512,-32768,-32512,-256])
        self.assertEqual(effect_pcm(6,raw),[0,2,32258,0,-2,-32258])
    def test_retail_effect_waveforms(self):
        root=Path(__file__).resolve().parents[1]/'assets/retail'
        bank_path=root/'blocks/06.bin'
        if not bank_path.exists():self.skipTest('Retail data not installed')
        bank=bank_path.read_bytes();effects=json.loads((root/'audio/effects.json').read_text())
        for event in [0,1,10]:
            record=next(e for e in effects['effects'] if e['event']==event)
            offset=struct.unpack_from('>I',bank,record['sample']*4)[0];count=struct.unpack_from('>I',bank,offset+20)[0]
            expected=effect_pcm(record['sample'],bank[offset+90:offset+90+count])
            with wave.open(str(root/'audio'/record['file'])) as wav:
                actual=list(struct.unpack('<'+'h'*wav.getnframes(),wav.readframes(wav.getnframes())))
            self.assertEqual(actual,expected)
            self.assertLess(abs(sum(actual)/len(actual)/32768),.02,'no decoder-induced DC offset in gun/footstep effects')
        other=root.parent/'is2-research/blocks/25.bin'
        if other.exists():
            other=other.read_bytes()
            for index in range(2,9):
                def sample(bank):
                    p=struct.unpack_from('>I',bank,index*4)[0];n=struct.unpack_from('>I',bank,p+20)[0];return bank[p+90:p+90+n]
                a=effect_pcm(index,sample(bank));b=[(-1 if v&1 else 1)*(v>>1)**2*2 for v in sample(other)]
                correlation=sum(x*y for x,y in zip(a,b))/math.sqrt(sum(x*x for x in a)*sum(y*y for y in b))
                self.assertGreater(correlation,.9998,'independently encoded IS2 copy matches recovered IS1 sound')
    def test_byte_deltas_wrap(self):
        blob=b'PDB!'+struct.pack('>I',4)+b'\xf0'+bytes([127,2,255,130])
        self.assertEqual(unpack_sample(blob),(bytes([127,129,128,2]),'PDB'))
    def test_word_deltas_big_endian(self):
        blob=b'PDW!'+struct.pack('>I',6)+b'\xfc'+bytes.fromhex('7fff0002ffff')
        self.assertEqual(unpack_sample(blob),(bytes.fromhex('7fff80018000'),'PDW'))
    def test_truncation(self):
        with self.assertRaises(ValueError):unpack_sample(b'PDB!'+struct.pack('>I',4)+b'\xf0\x01')
    def test_overlap_before_delta(self):
        blob=b'PDB!'+struct.pack('>I',4)+bytes([0x80,1,0,1])
        self.assertEqual(unpack_sample(blob)[0],bytes([1,2,3,4]))
    def test_retail_sound_pointers(self):
        root=Path(__file__).resolve().parents[1]/'assets/retail'
        if not (root/'audio/music.json').exists():self.skipTest('Retail data not installed')
        j=json.loads((root/'audio/music.json').read_text());self.assertEqual(j['schema'],2)
        self.assertEqual(j['intro_track'],6);self.assertEqual(len(j['modules'][6]['instruments']),14)
        for module in j['modules']:
            for ins in module['instruments']:
                if ins['kind']==4:self.assertEqual(len(ins['envelope']),10)
        b=(root/'blocks/07.bin').read_bytes();effects=json.loads((root/'audio/effects.json').read_text())
        for e in effects['effects']:
            self.assertEqual(e['sample'],struct.unpack_from('>i',b,0x7de8+4*e['event'])[0])
            self.assertEqual(e['pitch'],struct.unpack_from('>i',b,0x7e68+4*e['event'])[0])
    def test_raw_identity(self):
        self.assertEqual(unpack_sample(b'\x81\x00\xff'),(b'\x81\x00\xff','raw'))
if __name__=='__main__':unittest.main()
