import importlib.util
from pathlib import Path
import struct
import sys
import unittest

TOOLS=Path(__file__).resolve().parents[1]/'tools'
sys.path.insert(0,str(TOOLS))
import decode_art as art


def stream(size, body):
    return b'MSS!'+struct.pack('>I',size)+body

class CompressionTests(unittest.TestCase):
    def test_literals_and_partial_flags(self):
        data=stream(3,b'\xe0ABC')+b'padding'
        self.assertEqual(art.unpack_mss(data),(b'ABC',12))

    def test_flags_are_msb_first_and_overlap(self):
        # Three literals followed by nine bytes copied from distance three.
        self.assertEqual(art.unpack_mss(stream(12,b'\xe0ABC\x06\x03'))[0],b'ABCABCABCABC')

    def test_control_byte_rollover(self):
        self.assertEqual(art.unpack_mss(stream(9,b'\xffABCDEFGH\x80I'))[0],b'ABCDEFGHI')

    def test_rejects_bad_streams(self):
        bad=[b'',stream(0,b''),stream(art.MAX_EXPANDED+1,b''),
             stream(1,b''),stream(1,b'\x80'),stream(3,b'\x00\x00'),
             stream(3,b'\x00\x00\x00'),stream(3,b'\x00\x00\x01'),
             stream(2,b'\x80A\x00\x01')]
        for data in bad:
            with self.subTest(data=data):
                with self.assertRaises(ValueError):art.unpack_mss(data)

class PixelTests(unittest.TestCase):
    def test_hardware_rgb16_channels(self):
        self.assertEqual(art.rgb16_pixel(0xf800),(248,0,0))
        self.assertEqual(art.rgb16_pixel(0x003f),(0,252,0))
        self.assertEqual(art.rgb16_pixel(0x07c0),(0,0,248))
        self.assertEqual(art.rgb16_pixel(0xffff),(248,252,248))

    def test_hardware_rgb24_order(self):
        self.assertEqual(art.rgb24_pixel(0x12340056),(0x34,0x12,0x56))

    def test_cry_palette_and_bounds(self):
        table=art.cry_table()
        self.assertEqual(table['red'][8][12],115)
        self.assertEqual(art.cry_pixel(0x8800,table),(0,0,0))
        with self.assertRaises(ValueError):art.pixels16(b'\0',1,1,'cry16',table)
        with self.assertRaises(ValueError):art.pixels16(b'\0\0',1,1,'guessed',table)

    def test_indexed_alpha(self):
        palette=[(1,2,3,255)]*256;palette[0]=(0,0,0,0)
        image=art.pixels_indexed(b'\0\1',2,1,palette)
        self.assertEqual(image.getpixel((0,0)),(0,0,0,0))
        self.assertEqual(image.getpixel((1,0)),(1,2,3,255))
        with self.assertRaises(ValueError):art.pixels_indexed(b'\0',2,1,palette)

class LocalCartridgeTests(unittest.TestCase):
    def test_ballistic_sprites_are_particles_not_scenery(self):
        import json
        root=TOOLS.parent/'assets/retail'
        if not (root/'art/manifest.json').exists():self.skipTest('Local assets absent')
        entries={a['name']:a for a in json.loads((root/'art/manifest.json').read_text())['assets']}
        canonical=(root/'art'/entries['shot-rifle']['rgba']).read_bytes()
        for name in ['shot-rifle','shot-gatling','shot-rail']:
            e=entries[name]
            self.assertEqual((root/'art'/e['rgba']).read_bytes(),canonical)
            self.assertAlmostEqual(e['world_size'],1.4)
        for name in ['tree-conifer','tree-round','tree-leafless']:
            self.assertNotEqual((root/'art'/entries[name]['rgba']).read_bytes(),canonical)

    def test_effect_frames(self):
        import json
        root=TOOLS.parent/'assets/retail'
        if not (root/'art/manifest.json').exists():self.skipTest('Local assets absent')
        entries={a['name']:a for a in json.loads((root/'art/manifest.json').read_text())['assets']}
        for name,width in [('explosion-atlas',80),('smoke-atlas',32)]:
            e=entries[name];self.assertEqual((e['width'],e['height'],e['frames'],e['fps']),(width,width*10,10,10))
            data=(root/'art'/e['rgba']).read_bytes();self.assertEqual(data[3],0)
            self.assertTrue(any(data[3::4]));self.assertEqual(art.sha(data),e['rgba_sha256'])

    def test_known_world_resource_hashes(self):
        cart=TOOLS.parent/'assets/retail/cartridge.bin'
        if not cart.exists():self.skipTest('Optional local cartridge not imported')
        rom,_=art.validate_cartridge(cart.read_bytes())
        blocks,records=art.extract_blocks(rom)
        self.assertEqual(len(blocks),29)
        self.assertEqual(sum(r['encoding']=='MSS' for r in records),27)
        expected={
            0:'5f7178cbcc832d12775c0293e3172e9926bb4c3e35e1c659db6ce97778f995b4',
            1:'07e4c17fdf61f602256df6b63bbfbf1384f351e218ea7edd2a2ec796f5b4f83a',
            5:'722e7d2d059c30ffdf406cffec68936ec512058bb96392d95cbe39b414b0fcdf',
            11:'a89f0f6eee69f5be23f02ac0ef18faeddfc1a2f375a545864cd84df2067af73a',
            13:'ff4308d301c9c878406087cf5a5a8227edc9e6248f96719f324e9353de1f0de5',
        }
        for block,digest in expected.items():self.assertEqual(art.sha(blocks[block]),digest)

if __name__=='__main__':unittest.main()
