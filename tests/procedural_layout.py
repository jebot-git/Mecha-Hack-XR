"""Run directly: python3 tests/procedural_layout.py (no retail data required)."""
import copy
from collections import deque
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

TOOL = Path(__file__).resolve().parents[1] / 'tools/procedural_layout.py'
SPEC = importlib.util.spec_from_file_location('layout_tool', TOOL)
layout = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(layout)


class ProceduralLayout(unittest.TestCase):
    def test_many_seeds_sizes_and_missions(self):
        # Cover extreme dimensions, district boundaries, strike plot matching
        # and both extraction settings without requiring cartridge-derived data.
        for seed in range(384):
            cells = 2 + seed % 7
            for kind in layout.KINDS:
                recipe = layout.generate(seed, cells, 80+seed % 101, min(3, cells),
                                         kind, bool(seed % 2), 1+seed % 30)
                report = layout.validate(recipe)
                self.assertEqual(report['connected_nodes'], (cells+1)**2)
                self.assertEqual(report['clear_plots'], cells**2)
                self.assertEqual(report['reachable_objectives'], min(3, cells))
                self.assertEqual(report['mission_kind'], kind)
                self.assertGreater(len(recipe['roads']), len(recipe['nodes'])-1)
                # Independently traverse the emitted graph; every objective is
                # several edges from insertion and every node has a route.
                neighbors = [[] for _ in recipe['nodes']]
                for road in recipe['roads']:
                    a, b = road['nodes'];neighbors[a].append(b);neighbors[b].append(a)
                distances = {recipe['spawn']['node']: 0};queue = deque(distances)
                while queue:
                    a = queue.popleft()
                    for b in neighbors[a]:
                        if b not in distances:
                            distances[b] = distances[a]+1;queue.append(b)
                self.assertEqual(len(distances), len(recipe['nodes']))
                self.assertTrue(all(distances[z['node']] >= 3 for z in recipe['objectives']))
                if kind == 'strike':
                    self.assertEqual(len({z['plot'] for z in recipe['objectives']}), min(3, cells))

    def test_repeatable_versioned_and_varied(self):
        first = layout.generate(42)
        self.assertEqual(first, layout.generate(42))
        self.assertEqual(first, json.loads(json.dumps(first)))
        self.assertNotEqual(first['roads'], layout.generate(43)['roads'])
        self.assertNotEqual(first['plots'], layout.generate(43)['plots'])
        self.assertEqual(first['version'], 3)
        self.assertEqual(first['generator'], 'xorshift32-v3')
        # A change to seed semantics needs an intentional generator revision.
        digest = hashlib.sha256(json.dumps(first, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        self.assertEqual(digest, 'cab51567f613a4676b30488b754e0b3504bc92b0875245a80fcd7ea887ff379c')
        for seed in (0, 1, 2**32-1):
            self.assertEqual(layout.generate(seed), layout.generate(seed))

    def test_districts_and_road_hierarchy(self):
        recipe = layout.generate(42)
        self.assertEqual({p['role'] for p in recipe['plots']}, set(layout.ROLES))
        self.assertEqual({r['class'] for r in recipe['roads']}, {'arterial', 'local'})
        self.assertEqual({r['width'] for r in recipe['roads']}, {20, 28})
        xs = sorted({n[0] for n in recipe['nodes']})
        self.assertGreater(len({b-a for a, b in zip(xs, xs[1:])}), 1)
        for plot in recipe['plots']:
            district = recipe['districts'][plot['district']]
            self.assertEqual(plot['sources'], district['sources'])
            self.assertEqual(plot['role'], district['role'])
        self.assertTrue(any(p['sources'] == ['is1'] for p in recipe['plots']))
        self.assertTrue(any(p['sources'] == ['is2'] for p in recipe['plots']))

    def test_mission_options_do_not_change_map(self):
        a, b = layout.generate(42), layout.generate(42, mission_kind='recon', extraction=True)
        for field in ('nodes', 'roads', 'plots', 'districts', 'spawn'):
            self.assertEqual(a[field], b[field])
        self.assertTrue(b['mission']['extraction'])
        self.assertEqual(b['extraction']['node'], b['spawn']['node'])
        self.assertTrue(all(z['kind'] == 'recon' for z in b['objectives']))

    def test_legacy_version_one_import(self):
        # Geometry in old recipes remains playable without mission/district data.
        recipe = layout.generate(17, format_version=2)
        recipe['version'] = 1
        for field in ('generator', 'districts', 'mission'):
            recipe.pop(field)
        for road in recipe['roads']:
            road.pop('class')
        for plot in recipe['plots']:
            plot.pop('district')
        result = layout.validate(recipe)
        self.assertEqual(result['mission_kind'], 'eliminate')
        self.assertFalse(result['extraction'])
        ET.fromstring(layout.svg(recipe))
        for plot in recipe['plots']:
            plot.pop('role');plot.pop('sources');plot.pop('id')
        layout.validate(recipe)
        ET.fromstring(layout.svg(recipe))

    def test_version_two_generator_semantics_remain_available(self):
        recipe = layout.generate(42, format_version=2)
        digest = hashlib.sha256(json.dumps(recipe, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        self.assertEqual(digest, '8dba3e58fa0240d82c877009f0979b7a44bcd46b2482358ccce575b9d6aba6c2')
        self.assertNotIn('terrain', recipe)
        self.assertNotIn('encounters', recipe)
        layout.validate(recipe)

    def test_real_terrain_water_and_density_recipes(self):
        for biome in ('urban', 'grassland', 'desert', 'hills'):
            for water in ('none', 'lake', 'canal'):
                recipe = layout.generate(42, mission_kind='strike', extraction=True, terrain=biome, water=water)
                self.assertEqual(recipe['terrain']['biome'], biome)
                self.assertEqual(len(recipe['terrain']['water']), int(water != 'none'))
                self.assertGreater(sum(p['buildings'] for p in recipe['plots']), len(recipe['plots'])*3)
                protected = {z['plot'] for z in recipe['objectives']}
                self.assertTrue(all(recipe['plots'][p]['buildings'] > 0 for p in protected))
                for body in recipe['terrain']['water']:
                    a = body['bounds']
                    dry_crossings = 0
                    for road in recipe['roads']:
                        p, q = [recipe['nodes'][n] for n in road['nodes']];half = road['width']/2
                        b = [min(p[0],q[0])-half, min(p[1],q[1])-half, max(p[0],q[0])+half, max(p[1],q[1])+half]
                        dry_crossings += int(a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3])
                    if water == 'canal':
                        self.assertGreater(dry_crossings, 0)
                    for plot in recipe['plots']:
                        b = plot['bounds']
                        if plot['buildings']:
                            self.assertFalse(a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3])
        sparse = layout.generate(42, water='none', density=1)
        dense = layout.generate(42, water='none', density=9)
        self.assertGreater(sum(p['buildings'] for p in dense['plots']), sum(p['buildings'] for p in sparse['plots'])*4)
        self.assertEqual(dense['nodes'], sparse['nodes'])
        self.assertEqual(dense['roads'], sparse['roads'])

    def test_invalid_physical_terrain_density_and_encounters(self):
        for mutate, message in [
            (lambda r: r['terrain'].update(biome='lava'), 'biome'),
            (lambda r: r['terrain'].update(amplitude=25), 'amplitude'),
            (lambda r: r['terrain'].update(amplitude=float('nan')), 'amplitude'),
            (lambda r: r['terrain']['water'][0].update(level=1), 'level or depth'),
            (lambda r: r['terrain']['water'][0].update(depth=0), 'level or depth'),
            (lambda r: r['terrain']['water'].append(copy.deepcopy(r['terrain']['water'][0])), 'water regions overlap'),
            (lambda r: r['plots'][0].update(buildings=17), 'density'),
            (lambda r: r['encounters'].update(ground_per_zone=0), 'ground encounter'),
            (lambda r: r['encounters'].update(air_units=9), 'air encounter'),
        ]:
            with self.subTest(message=message):
                recipe = layout.generate(42);mutate(recipe);self.reject(recipe, message)
        recipe = layout.generate(42, mission_kind='strike', water='none')
        recipe['plots'][recipe['objectives'][0]['plot']]['buildings'] = 0
        self.reject(recipe, 'empty parcel')
        recipe = layout.generate(42, water='none')
        recipe['terrain']['water'] = [{'bounds': recipe['plots'][0]['bounds'][:], 'level': -1, 'depth': 6}]
        self.reject(recipe, 'parcel overlaps water')

    def test_district_source_order_is_not_semantic(self):
        recipe = layout.generate(42)
        for plot in recipe['plots']:
            plot['sources'].reverse()
        layout.validate(recipe)

    def reject(self, recipe, message):
        with self.assertRaisesRegex(ValueError, message):
            layout.validate(recipe)

    def test_disconnected_roads(self):
        recipe = layout.generate(17)
        recipe['nodes'].append([0, 1])
        self.reject(recipe, 'disconnected')

    def test_blocked_corridor(self):
        recipe = layout.generate(17)
        x, z = recipe['nodes'][0]
        recipe['plots'][0]['bounds'] = [x-8, z-8, x+8, z+8]
        self.reject(recipe, 'overlaps a road')

    def test_invalid_geometry_and_metadata(self):
        for mutate, message in [
            (lambda r: r['roads'][0].update(width=15), 'too narrow'),
            (lambda r: r['roads'][0].update(width=float('nan')), 'too narrow'),
            (lambda r: r['roads'][0].update(width=10**1000), 'too narrow'),
            (lambda r: r['roads'][0].update(width=61), 'too narrow'),
            (lambda r: r['roads'][0].update(nodes=[0, 999]), 'endpoint'),
            (lambda r: r['objectives'][0].update(node=0), 'share a node'),
            (lambda r: r['spawn'].update(radius=1), 'too small'),
            (lambda r: r['bounds'].__setitem__(0, -1001), 'world bounds'),
            (lambda r: r['plots'][0].update(bounds=[0, 0, 0, 0]), 'plot bounds'),
            (lambda r: r['plots'][0].update(id=100), 'plot id'),
            (lambda r: r['plots'][0].update(district=123), 'unknown plot district'),
            (lambda r: r['plots'][0].update(sources=['is3']), 'preferences'),
            (lambda r: r['districts'][1].update(id=0), 'district id'),
            (lambda r: r['districts'][1].update(id=64), 'district id'),
            (lambda r: r.update(districts=[]), 'invalid districts'),
            (lambda r: r['districts'][0].update(role='water'), 'preferences'),
            (lambda r: r.update(seed=-1), 'seed'),
            (lambda r: r.update(version=True), 'schema/version'),
        ]:
            with self.subTest(message=message):
                recipe = copy.deepcopy(layout.generate(17));mutate(recipe)
                self.reject(recipe, message)

    def test_invalid_missions_and_strike_targets(self):
        for mutate, message in [
            (lambda r: r['mission'].update(kind='escort'), 'mission plan'),
            (lambda r: r['mission'].update(extraction=1), 'mission plan'),
            (lambda r: r['mission'].update(hold_seconds=0), 'hold seconds'),
            (lambda r: r['mission'].update(hold_seconds=float('inf')), 'hold seconds'),
            (lambda r: r['objectives'][0].update(kind='recon'), 'objective kind'),
            (lambda r: r['objectives'][0].update(plot=999), 'strike plot'),
            (lambda r: r['objectives'][1].update(plot=r['objectives'][0]['plot']), 'strike plot'),
            (lambda r: r['extraction'].update(node=r['objectives'][0]['node']), 'insertion node'),
            (lambda r: r.pop('extraction'), 'zone node'),
        ]:
            with self.subTest(message=message):
                recipe = layout.generate(17, mission_kind='strike', extraction=True)
                mutate(recipe);self.reject(recipe, message)
        recipe = layout.generate(17)
        recipe['extraction'] = recipe['spawn'].copy()
        self.reject(recipe, 'disabled extraction')
        recipe = layout.generate(17, mission_kind='strike')
        zone = recipe['objectives'][0]
        node = recipe['nodes'][zone['node']]
        far = max(range(len(recipe['plots'])), key=lambda i: sum(abs(recipe['plots'][i]['bounds'][k]-node[k % 2]) for k in range(4)))
        zone['plot'] = far
        self.reject(recipe, 'not adjacent')

    def test_runtime_budgets(self):
        recipe = layout.generate(1, cells=8, spacing=180, objectives=8)
        self.assertEqual(len(recipe['nodes']), 81)
        self.assertEqual(len(recipe['plots']), 64)
        layout.validate(recipe)
        recipe['nodes'].append([1, 1]);self.reject(recipe, 'node budget')

    def test_generation_input_bounds(self):
        for kwargs in ({'seed': -1}, {'seed': 2**32}, {'seed': True},
                       {'cells': 1}, {'cells': 9}, {'spacing': 79}, {'spacing': 181},
                       {'objectives': 0}, {'objectives': 5}, {'mission_kind': 'escort'},
                       {'extraction': 1}, {'hold_seconds': True}, {'hold_seconds': 31}, {'hold_seconds': 10**1000},
                       {'terrain': 'lava'}, {'water': 'ocean'}, {'density': 0}, {'density': 17},
                       {'ground_per_zone': 0}, {'air_units': 9}, {'format_version': 1}):
            with self.subTest(kwargs=kwargs), self.assertRaises(ValueError):
                layout.generate(**({'seed': 1} | kwargs))

    def test_cli_roundtrip_and_svg(self):
        with tempfile.TemporaryDirectory() as folder:
            path, preview = Path(folder)/'layout.json', Path(folder)/'layout.svg'
            subprocess.run([sys.executable, str(TOOL), '--seed', '42', '--mission-kind', 'strike',
                            '--extract', '--output', str(path), '--svg', str(preview)], check=True)
            self.assertEqual(json.loads(path.read_text()), layout.generate(42, mission_kind='strike', extraction=True))
            self.assertEqual(ET.fromstring(preview.read_text()).tag, '{http://www.w3.org/2000/svg}svg')
            result = subprocess.run([sys.executable, str(TOOL), '--validate', str(path)], check=True, capture_output=True, text=True)
            self.assertEqual(json.loads(result.stdout)['reachable_objectives'], 3)
            self.assertEqual(json.loads(result.stdout)['mission_kind'], 'strike')
            self.assertTrue(json.loads(result.stdout)['extraction'])


if __name__ == '__main__':
    unittest.main()
