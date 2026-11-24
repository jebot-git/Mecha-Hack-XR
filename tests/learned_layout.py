"""Portable learning/generation tests; synthetic maps avoid cartridge fixtures."""
import copy,json,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from tools.layout_learning.corpus import GRID,TOKENS
from tools.layout_learning.model import fit,validate_model
from tools.layout_learning.generate import generate
from tools.layout_learning.segments import compose
from tools import procedural_layout as layout

def fixtures(dominant='urban:2'):
    maps=[]
    for i in range(6):
        states=[TOKENS.index(dominant) if (x//2+z//2+i)%3 else 0 for z in range(GRID) for x in range(GRID)]
        maps.append(dict(id=f'fixture-{i}',campaign='is1' if i<3 else 'is2',states=states,densities=[2.0 if s else 0 for s in states],
            road_edges=[('0:0:1',1),('1:0:1',0),('0:1:1',1)],road_widths=[8,10,12],span=[800+i*10,700+i*15],
            spawn=[.1,.1],encounter_distances=[.2,.4,.5,.6],ground_per_zone=4,air_units=2,biome='urban'))
    return maps

class LearnedLayout(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.model,cls.report=fit(fixtures())
    def test_real_fit_and_grouped_validation(self):
        self.assertEqual(self.model['training_maps'],6)
        self.assertEqual(sum(self.model['tables']['global_counts']),6*64)
        self.assertEqual(self.report['folds'],[f'fixture-{i}' for i in range(6)])
        self.assertLessEqual(self.report['selected']['bits_per_cell'],self.report['baseline_bits_per_cell'])
        self.assertEqual(self.model,fit(fixtures())[0])
        altered=fit(fixtures('storage:2'))[0]
        a=generate(self.model,42);b=generate(altered,42)
        self.assertNotEqual(self.model['tables']['global_counts'],altered['tables']['global_counts'])
        self.assertGreater(sum(p['role']=='urban' for p in a['plots']),sum(p['role']=='urban' for p in b['plots']))
        self.assertGreater(sum(p['role']=='storage' for p in b['plots']),sum(p['role']=='storage' for p in a['plots']))
    def test_deterministic_native_recipes(self):
        for seed in [0,1,42,0xffffffff]:
            for cells in (2,4,6,8):
                for kind in layout.KINDS:
                    for water in ('none','lake','canal'):
                        recipe=generate(self.model,seed,cells,kind,water=water)
                        layout.validate(recipe)
                        self.assertEqual(recipe,generate(self.model,seed,cells,kind,water=water))
                        self.assertEqual(recipe,json.loads(json.dumps(recipe)))
                        self.assertEqual(len(recipe['nodes']),(cells+1)**2)
                        self.assertGreater(len(recipe['roads']),len(recipe['nodes'])-1)
                        self.assertEqual(recipe['learning']['model_sha256'],self.model['sha256'])
    def test_bad_inputs_and_tampering(self):
        for seed in (-1,1<<32,True):
            with self.assertRaises(ValueError):generate(self.model,seed)
        for cells in (1,9,True):
            with self.assertRaises(ValueError):generate(self.model,1,cells)
        broken=copy.deepcopy(self.model);broken['tables']['global_counts'][0]+=1
        with self.assertRaisesRegex(ValueError,'checksum'):validate_model(broken)
        with self.assertRaises(ValueError):generate(self.model,1,mission_kind='escort')
        with self.assertRaises(ValueError):generate(self.model,1,terrain='lava')
    def test_segment_composition(self):
        self.assertEqual(sum(self.model['composition']['counts']),24)
        self.assertEqual(sum(sum(v) for v in self.model['composition']['transitions'].values()),48)
        for segments in (1,2,4,6,8):
            for kind in layout.KINDS:
                for water in ('none','lake','canal'):
                    r=compose(self.model,42,cells=4,mission_kind=kind,segments=segments,map_scale=2,water=water)
                    self.assertEqual(r,compose(self.model,42,cells=4,mission_kind=kind,segments=segments,map_scale=2,water=water))
                    self.assertEqual(r['version'],4);self.assertEqual(len(r['segments']),segments)
                    self.assertLessEqual(sum(p['buildings'] for p in r['plots']),2048)
                    self.assertLessEqual(r['encounters']['ground_per_zone']*len(r['objectives']),64)
                    self.assertEqual(r['objectives'][-1]['segment'],segments-1)
                    last=r['segments'][-1]['node_range'];self.assertTrue(last[0]<=r['extraction']['node']<sum(last))
                    if segments>=4:self.assertGreater(max(abs(v) for v in r['bounds']),1000)
        for arrangement in ('grid','chain'):
            layout.validate(compose(self.model,0,segments=3,map_scale=1.5,arrangement=arrangement,ordered=False,extraction=False))
    def test_segment_validation(self):
        recipe=compose(self.model,0,segments=4)
        mutations=[lambda r:r['segments'][0].update(node_range=[0,999]),
                   lambda r:r['segments'][1].update(bounds=r['segments'][0]['bounds']),
                   lambda r:r['objectives'][0].update(segment=2),
                   lambda r:r['connections'][0].update(nodes=[0,1]),
                   lambda r:r['roads'][0].update(connection=0),
                   lambda r:r.update(extraction=dict(node=r['spawn']['node'],radius=8))]
        for mutate in mutations:
            broken=copy.deepcopy(recipe);mutate(broken)
            with self.assertRaises(ValueError):layout.validate(broken)
        for kwargs in [dict(segments=0),dict(segments=9),dict(segments=True),dict(map_scale=3),dict(map_scale=float('nan')),dict(arrangement='spiral'),dict(campaign='unknown')]:
            with self.assertRaises(ValueError):compose(self.model,0,**kwargs)
        legacy=copy.deepcopy(self.model);legacy['version']=1;legacy.pop('composition')
        import hashlib
        legacy['sha256']=hashlib.sha256(json.dumps({k:v for k,v in legacy.items() if k!='sha256'},sort_keys=True,separators=(',',':')).encode()).hexdigest()
        validate_model(legacy);layout.validate(generate(legacy,42))
        with self.assertRaises(ValueError):compose(legacy,42)
    def test_continuous_policy(self):
        r=compose(self.model,88,segments=4);policy=r['reinforcements']
        self.assertEqual(policy['mode'],'continuous')
        self.assertEqual(policy['ground_limit'],r['encounters']['ground_per_zone']*len(r['objectives']))
        self.assertEqual(policy['air_limit'],r['encounters']['air_units'])
        self.assertNotIn('reinforcements',compose(self.model,88,segments=4,continuous=False))
        for key,value in [('ground_limit',65),('air_limit',9),('kill_quota',0),('ground_limit',1),('activation_range',99),('interval',4),('ground_limit',True),('per_zone',3),('mode','unknown')]:
            broken=copy.deepcopy(r);broken['reinforcements'][key]=value
            with self.assertRaises(ValueError):layout.validate(broken)
        legacy=generate(self.model,1);legacy['reinforcements']=policy
        with self.assertRaises(ValueError):layout.validate(legacy)
    def test_campaign_style_and_extraction(self):
        for campaign in ('is1','is2'):
            r=generate(self.model,88,campaign=campaign,extraction=False)
            self.assertTrue(all(p['sources']==[campaign] for p in r['plots']))
            self.assertNotIn('extraction',r)
            self.assertFalse(r['mission']['extraction'])

if __name__=='__main__':unittest.main()
