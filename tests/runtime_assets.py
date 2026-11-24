"""Runtime-only closure checks and isolated corruption regressions."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('audit_runtime_assets', ROOT / 'tools/audit_runtime_assets.py')
AUDITOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDITOR)


class RuntimeAssets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = ROOT / 'assets/retail'
        if not (cls.source / 'gameplay/missions.json').is_file():
            raise unittest.SkipTest('locally decoded cartridges unavailable')

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.retail = Path(self.temporary.name) / 'assets/retail'
        # Real directories plus file symlinks let each test remove/replace only
        # its own fixture. Cartridge/research blocks are deliberately absent.
        shutil.copytree(self.source, self.retail, copy_function=os.symlink,
                        ignore=shutil.ignore_patterns('blocks', 'cartridge.bin'))
        (self.retail.parent / 'system').symlink_to(self.source.parent / 'system', target_is_directory=True)
        (self.retail / 'manifest.json').unlink(missing_ok=True)

    def change_json(self, relative, change):
        path = self.retail / relative
        value = json.loads(path.read_text())
        change(value)
        path.unlink()
        path.write_text(json.dumps(value))

    def failures(self):
        return '\n'.join(AUDITOR.audit(self.retail)['errors'])

    def test_runtime_only_data_passes_without_rom_or_research_blocks(self):
        result = AUDITOR.audit(self.retail)
        self.assertEqual(result['errors'], [])
        self.assertEqual(result['counts']['mission_load_paths'], 52)
        self.assertGreaterEqual(result['counts']['catalog_model_references'], 396)
        self.assertEqual(result['counts']['animation_rigs'], 3)
        self.assertEqual(result['counts']['music_modules'], 7)

    def test_unmanifested_bridge_fragment_is_rejected_even_if_file_exists(self):
        self.change_json('models/manifest.json', lambda j: j['models'].pop('fragment-223860'))
        errors = self.failures()
        self.assertIn('destruction references fragment-223860, absent from models/manifest.json', errors)

    def test_missing_destruction_file_is_rejected(self):
        for prefix in ('models', 'is2/shared/models'):
            (self.retail / prefix / 'fragment-223892.json').unlink(missing_ok=True)
        self.assertIn('missing runtime file models/fragment-223892.json', self.failures())

    def test_shared_precedence_and_per_file_fallback(self):
        path = self.retail / 'is2/shared/gameplay/world-226b8.json'
        self.assertTrue(path.exists())
        path.unlink()
        self.assertEqual(AUDITOR.audit(self.retail)['errors'], [])
        path.write_text('{broken override')
        self.assertIn('is2/shared/gameplay/world-226b8.json: invalid JSON', self.failures())

    def test_wreck_geometry_index_is_checked(self):
        catalog = json.loads((self.retail / 'is2/gameplay/missions.json').read_text())
        wreck = catalog['objects'][0]['wreck']
        self.change_json(f'is2/gameplay/{wreck}.json', lambda j: j['faces'][0]['indices'].__setitem__(0, len(j['vertices'])))
        self.assertIn(f'is2/gameplay/{wreck}.json: face 0 has invalid polygon indices', self.failures())

    def test_generator_template_references_are_checked(self):
        def corrupt(j):
            for mission in j['missions']:
                for spawn in mission['spawns']:
                    if spawn['type'] == 4:
                        spawn['parameters'][10:12] = [255, 255]
                        return
            self.fail('fixture lacks generators')
        self.change_json('is2/gameplay/missions.json', corrupt)
        self.assertIn('invalid generator template spawn 65535', self.failures())

    def test_cargo_template_references_are_checked(self):
        def corrupt(j):
            for mission in j['missions']:
                for spawn in mission['spawns']:
                    if spawn['type'] == 12:
                        # words[6]=1 selects source index words[0], whose bytes
                        # live at parameter offset4 in the native loader.
                        spawn['parameters'][4:6] = [255, 255]
                        spawn['parameters'][16:18] = [0, 1]
                        return
            self.fail('fixture lacks IS2 cargo carriers')
        self.change_json('is2/gameplay/missions.json', corrupt)
        self.assertIn('invalid cargo template spawn 65535', self.failures())

    def test_art_alias_and_binary_length_are_checked(self):
        self.change_json('is2/art/manifest.json', lambda j: j['assets'].__setitem__(0, dict(j['assets'][0], name='unreachable-thumbnail')))
        path = self.retail / 'art/hud-font.rgba'
        path.unlink()
        path.write_bytes(b'bad')
        errors = self.failures()
        self.assertIn('missing renderer art name is2-mission-thumbnail-01', errors)
        self.assertIn('art/hud-font.rgba has 3 bytes; expected', errors)

    def test_animation_range_and_held_model_references_are_checked(self):
        self.change_json('models/animation-rigs.json', lambda j: j['rigs']['satyr']['parts'][0]['ranges'].append([0, 32769]))
        self.change_json('models/rig.json', lambda j: j['held'][0].__setitem__('model', 'missing-weapon'))
        errors = self.failures()
        self.assertIn('satyr has invalid triangle range', errors)
        self.assertIn('held model missing-weapon absent from model manifest', errors)

    def test_campaign_cannot_leave_weapons_unobtainable(self):
        self.change_json('is2/art/frontend.json', lambda j: j['progression'].__setitem__('initial_weapons', 2))
        self.assertIn('campaign leaves native weapons unobtainable', self.failures())

    def test_missing_music_sample_and_cockpit_truncation_are_checked(self):
        self.change_json('audio/music.json', lambda j: next(i for i in j['modules'][0]['instruments'] if i['kind'] != 4).__setitem__('sample_file', 'missing-sample.bin'))
        path = self.retail / 'cockpit/cockpit.mesh'
        header = path.read_bytes()[:12]
        path.unlink()
        path.write_bytes(header)
        errors = self.failures()
        self.assertIn('missing runtime file audio/missing-sample.bin', errors)
        self.assertIn('cockpit/cockpit.mesh has 12 bytes; expected', errors)

    def test_native_sound_callers_require_playable_manifest_entries(self):
        self.change_json('is2/audio/effects.json', lambda j: j.__setitem__('effects', [e for e in j['effects'] if e['event'] != 27]))
        self.change_json('audio/effects.json', lambda j: next(e for e in j['effects'] if e['event'] == 14).__setitem__('stop_only', True))
        errors = self.failures()
        self.assertIn('is2/audio/effects.json: native sound caller event 27 has no playable, non-stop effect entry', errors)
        self.assertIn('audio/effects.json: native sound caller event 14 has no playable, non-stop effect entry', errors)

    def test_cli_reports_failure_and_writes_json(self):
        (self.retail / 'audio/score-14.bin').unlink()
        output = Path(self.temporary.name) / 'report.json'
        result = subprocess.run([sys.executable, str(ROOT / 'tools/audit_runtime_assets.py'), '--retail', str(self.retail), '--output', str(output)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertEqual(json.loads(result.stdout), json.loads(output.read_text()))
        self.assertIn('missing runtime file audio/score-14.bin', result.stdout)

    def test_original_dot_two_bundle_exposes_bridge_omissions(self):
        old = ROOT / 'artifacts/test-clients-20261106.2/runtime/assets/retail'
        if not old.exists():
            self.skipTest('historical .2 bundle unavailable')
        manifest = json.loads((old / 'models/manifest.json').read_text())
        if 'fragment-223860' in manifest['models']:
            self.skipTest('historical bundle has been regenerated')
        errors = '\n'.join(AUDITOR.audit(old)['errors'])
        self.assertIn('destruction references fragment-223860, absent', errors)
        self.assertIn('destruction references fragment-223892, absent', errors)


if __name__ == '__main__':
    unittest.main()
