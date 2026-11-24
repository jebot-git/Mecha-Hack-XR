import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch
import hashlib

source = Path(__file__).resolve().parents[1]/'tools/import_cartridge.py'
spec = importlib.util.spec_from_file_location('import_cartridge', source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class ImporterTests(unittest.TestCase):
    def test_truncated(self):
        with self.assertRaisesRegex(ValueError, 'Expected'):
            module.decode(b'bad')

    def test_unsupported(self):
        with self.assertRaisesRegex(ValueError, 'Unsupported'):
            module.decode(bytes(module.SIZE))

    def test_normalization_and_provenance(self):
        # Synthetic cart validates endian handling without requiring retail data in CI.
        data = bytearray(module.SIZE)
        data[module.MODULE_START:module.MODULE_END] = b'TEST MODULE'.ljust(module.MODULE_END-module.MODULE_START, b' ')
        data = bytes(data)
        digest = hashlib.sha256(data).hexdigest()
        with patch.object(module, 'SHA256', digest):
            normalized, manifest = module.decode(data)
            self.assertEqual(normalized, data)
            self.assertEqual(manifest['module'], 'TEST MODULE')
            self.assertEqual(len(manifest['regions']), 32)
            swapped = bytearray(data)
            swapped[0::2], swapped[1::2] = data[1::2], data[0::2]
            normalized, manifest = module.decode(bytes(swapped))
            self.assertEqual(normalized, data)
            self.assertIn('word-swapped', manifest['input_order'])
            self.assertEqual(manifest['regions'][0]['sha256'], hashlib.sha256(data[:65536]).hexdigest())

if __name__ == '__main__':
    unittest.main()
