#!/usr/bin/env python3
"""Generate a deterministic, flat procedural mission recipe (no retail data).

    python3 tools/procedural_layout.py --seed 42 --mission-kind strike \
        --extract --output /tmp/layout.json --svg /tmp/layout.svg
    python3 tools/procedural_layout.py --validate /tmp/layout.json

Recipes describe connected roads, coherent scenery districts and mission zones
in native world metres. Runtime placement must still check actual model bounds.
Version 3 adds dense parcels, biome height fields and blocked water with dry
road crossings; versions 1/2 remain accepted. The seeded xorshift32 stream is explicitly
versioned, independent of Python's random module and container iteration order.
"""

import argparse
from collections import deque
import json
import math
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))

VERSION = 3
PILOT_RADIUS = 3.5
KINDS = ('eliminate', 'strike', 'recon')
ROLES = ('industrial', 'storage', 'urban')
SOURCES = ('is1', 'is2')


class Random:
    def __init__(self, seed):
        self.state = seed or 0x49535852

    def below(self, limit):
        if limit <= 0:
            raise ValueError('random bound must be positive')
        x = self.state
        x ^= (x << 13) & 0xffffffff
        x ^= x >> 17
        x ^= (x << 5) & 0xffffffff
        self.state = x & 0xffffffff
        return self.state % limit


def _distances(adjacency, start):
    distances = {start: 0}
    queue = deque([start])
    while queue:
        current = queue.popleft()
        for node in adjacency[current]:
            if node not in distances:
                distances[node] = distances[current] + 1
                queue.append(node)
    return distances


def _generate_v2(seed, cells=4, spacing=120, objectives=3, mission_kind='eliminate',
             extraction=False, hold_seconds=4):
    """Build a connected recipe with clear mission zones and mixed districts."""
    if type(seed) is not int or not 0 <= seed <= 0xffffffff:
        raise ValueError('seed must be an unsigned 32-bit integer')
    if type(cells) is not int or not 2 <= cells <= 8:
        raise ValueError('cells must be between 2 and 8')
    if type(spacing) is not int or not 80 <= spacing <= 180:
        raise ValueError('spacing must be between 80 and 180 metres')
    if type(objectives) is not int or not 1 <= objectives <= cells:
        raise ValueError('objectives must be between 1 and cells')
    if mission_kind not in KINDS:
        raise ValueError('unknown mission kind')
    if type(extraction) is not bool:
        raise ValueError('extraction must be boolean')
    if type(hold_seconds) not in (int, float) or not 1 <= hold_seconds <= 30 or not math.isfinite(hold_seconds):
        raise ValueError('hold seconds must be between 1 and 30')
    rng = Random(seed)
    half = cells * spacing / 2
    jitter = min(10, spacing // 10)
    axes = []
    for _ in range(2):
        axis = [-half]
        axis += [i * spacing-half+rng.below(2*jitter+1)-jitter for i in range(1, cells)]
        axis.append(half)
        axes.append(axis)
    xs, zs = axes
    nodes = [[x, z] for z in zs for x in xs]
    side = cells + 1
    neighbors = [[] for _ in nodes]
    for a in range(len(nodes)):
        for b in (a+1 if a % side < cells else -1, a+side):
            if 0 <= b < len(nodes):
                neighbors[a].append(b)
                neighbors[b].append(a)
    edges, visited, stack = set(), {0}, [0]
    while stack:
        a = stack[-1]
        options = [b for b in neighbors[a] if b not in visited]
        if not options:
            stack.pop()
            continue
        b = options[rng.below(len(options))]
        edges.add(tuple(sorted((a, b))))
        visited.add(b)
        stack.append(b)
    mid = cells // 2
    def arterial(a, b):
        return (a//side == mid and b//side == mid) or (a % side == mid and b % side == mid)
    # Major avenues cross the whole town. Remaining loops vary with the seed,
    # avoiding a single long maze as the only route between combat zones.
    loop_chance = 20 + rng.below(26)
    for a, choices in enumerate(neighbors):
        for b in choices:
            if a < b and (arterial(a, b) or rng.below(100) < loop_chance):
                edges.add((a, b))
    if len(edges) == len(nodes)-1:
        extra = [(a, b) for a, choices in enumerate(neighbors) for b in choices
                 if a < b and (a, b) not in edges]
        edges.add(extra[rng.below(len(extra))])
    roads = [{'nodes': [a, b], 'width': 28 if arterial(a, b) else 20,
              'class': 'arterial' if arterial(a, b) else 'local'} for a, b in sorted(edges)]
    district_rotation = rng.below(3)
    districts = []
    for i in range(4):
        sources = ['is1'] if i == 1 else ['is2'] if i == 2 else ['is1', 'is2']
        districts.append({'id': i, 'role': ROLES[(i+district_rotation) % 3], 'sources': sources})
    plots = []
    for z in range(cells):
        for x in range(cells):
            district = districts[int(x >= cells/2)+2*int(z >= cells/2)]
            cx, cz = (xs[x]+xs[x+1])/2, (zs[z]+zs[z+1])/2
            hx, hz = (xs[x+1]-xs[x]-40)/2, (zs[z+1]-zs[z]-40)/2
            hx -= rng.below(min(9, int(hx-4)+1))
            hz -= rng.below(min(9, int(hz-4)+1))
            plots.append({'id': len(plots), 'bounds': [cx-hx, cz-hz, cx+hx, cz+hz],
                          'district': district['id'], 'sources': district['sources'][:],
                          'role': district['role']})
    adjacency = [[] for _ in nodes]
    for a, b in sorted(edges):
        adjacency[a].append(b)
        adjacency[b].append(a)
    distances = _distances(adjacency, 0)
    candidates = [i for i in distances if distances[i] >= 3]
    selected, selected_distances, used_plots, zones = [], [], set(), []
    while len(zones) < objectives:
        options = []
        for node in candidates:
            if node in selected:
                continue
            nx, nz = node % side, node//side
            adjacent = [z*cells+x for z in (nz-1, nz) for x in (nx-1, nx)
                        if 0 <= x < cells and 0 <= z < cells and z*cells+x not in used_plots]
            if mission_kind == 'strike' and not adjacent:
                continue
            separation = min((d[node] for d in selected_distances), default=distances[node])
            options.append((separation*3+distances[node], node, adjacent))
        if not options:
            raise ValueError('not enough distinct reachable objective sites')
        options.sort(key=lambda item: (-item[0], item[1]))
        _, node, adjacent = options[rng.below(min(len(options), 3))]
        selected.append(node)
        selected_distances.append(_distances(adjacency, node))
        zone = {'node': node, 'radius': 8,
                'kind': 'encounter' if mission_kind == 'eliminate' else mission_kind}
        if mission_kind == 'strike':
            adjacent.sort(key=lambda i: (plots[i]['role'] == 'urban', i))
            chosen = adjacent[rng.below(min(len(adjacent), 2))]
            zone['plot'] = chosen
            used_plots.add(chosen)
        zones.append(zone)
    recipe = {'schema': 'iron-procedural-layout', 'version': 2, 'seed': seed,
              'generator': 'xorshift32-v2', 'units': 'metres', 'plane': 'xz', 'ground_y': 0,
              'bounds': [-half-24, -half-24, half+24, half+24],
              'pilot_radius': PILOT_RADIUS, 'nodes': nodes, 'roads': roads,
              'districts': districts, 'plots': plots, 'spawn': {'node': 0, 'radius': 8},
              'mission': {'kind': mission_kind, 'extraction': extraction, 'hold_seconds': hold_seconds},
              'objectives': zones}
    if extraction:
        recipe['extraction'] = {'node': 0, 'radius': 8}
    validate(recipe)
    return recipe



def generate(seed, cells=4, spacing=120, objectives=3, mission_kind='eliminate',
             extraction=False, hold_seconds=4, terrain='auto', water='auto',
             density=9, ground_per_zone=6, air_units=3, format_version=3):
    """Generate a dense playable town with physical biome terrain and water."""
    if type(format_version) is not int or format_version not in (2, 3):
        raise ValueError('format version must be 2 or 3')
    if terrain not in ('auto', 'urban', 'grassland', 'desert', 'hills'):
        raise ValueError('unknown terrain biome')
    if water not in ('auto', 'none', 'lake', 'canal'):
        raise ValueError('unknown water layout')
    if type(density) is not int or not 1 <= density <= 16:
        raise ValueError('density must be between 1 and 16 buildings per plot')
    if type(ground_per_zone) is not int or not 1 <= ground_per_zone <= 12:
        raise ValueError('ground actors per zone must be between 1 and 12')
    if type(air_units) is not int or not 0 <= air_units <= 8:
        raise ValueError('air units must be between 0 and 8')
    recipe = _generate_v2(seed, cells, spacing, objectives, mission_kind, extraction, hold_seconds)
    if format_version == 2:
        return recipe
    if ground_per_zone*objectives > 64:
        raise ValueError('ground encounter budget exceeds 64 actors')
    biome = ('urban', 'grassland', 'desert', 'hills')[(seed >> 1) % 4] if terrain == 'auto' else terrain
    water_kind = ('canal', 'lake', 'none')[seed % 3] if water == 'auto' else water
    recipe['version'] = 3
    recipe['generator'] = 'xorshift32-v3'
    regions = []
    side = cells+1
    if water_kind == 'canal':
        column = 1+(seed//7) % (cells-1)
        x = recipe['nodes'][column][0]
        regions.append({'bounds': [x+15, recipe['bounds'][1]+8, x+29, recipe['bounds'][3]-8],
                        'level': -1, 'depth': 6})
        # Reserve a genuine navigational obstacle beside the avenue, with a
        # bank before the next parcel. Horizontal roads remain dry crossings.
        for i, plot in enumerate(recipe['plots']):
            if i % cells != column:
                continue
            b = plot['bounds'];next_x = recipe['nodes'][column+1][0]
            extra = max(0, next_x-x-32-20-8)
            left = min(max(0, b[0]-x-20), extra)
            right = min(max(0, next_x-20-b[2]), extra-left)
            b[0], b[2] = x+32+left, next_x-20-right
    if water_kind == 'lake':
        protected = {zone['plot'] for zone in recipe['objectives'] if 'plot' in zone}
        candidates = [p for p in recipe['plots'] if p['id'] not in protected]
        if not candidates:
            raise ValueError('no non-objective parcel available for a lake')
        chosen = candidates[(seed//11) % len(candidates)]
        b = chosen['bounds'];inset = min(2, (min(b[2]-b[0], b[3]-b[1])-4)/2)
        regions.append({'bounds': [b[0]+inset, b[1]+inset, b[2]-inset, b[3]-inset],
                        'level': -1, 'depth': 6})
        chosen['buildings'] = 0
    for plot in recipe['plots']:
        if plot.get('buildings') == 0:
            continue
        b = plot['bounds']
        capacity = max(1, int((b[2]-b[0])/18))*max(1, int((b[3]-b[1])/18))
        plot['buildings'] = min(density, capacity, 768//len(recipe['plots']))
    recipe['terrain'] = {'biome': biome, 'amplitude': {'urban': 0, 'grassland': 3, 'desert': 5, 'hills': 18}[biome],
                         'water': regions, 'water_layout': water_kind}
    recipe['encounters'] = {'ground_per_zone': ground_per_zone, 'air_units': air_units}
    validate(recipe)
    return recipe


def validate(recipe):
    """Validate structural budgets, clear roads and actionable mission plans."""
    def require(ok, message):
        if not ok:
            raise ValueError(message)
    def number(value):
        if type(value) not in (int, float):
            return False
        try:
            return math.isfinite(value)
        except OverflowError:
            return False
    def numbers(value, length):
        return isinstance(value, list) and len(value) == length and all(number(v) for v in value)
    def rect(value):
        return numbers(value, 4) and value[0] < value[2] and value[1] < value[3]
    def overlap(a, b):
        return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]
    def sources(value):
        return isinstance(value, list) and 1 <= len(value) <= 2 and all(v in SOURCES for v in value) and len(set(value)) == len(value)
    require(isinstance(recipe, dict), 'layout must be an object')
    require(recipe.get('schema') == 'iron-procedural-layout' and type(recipe.get('version')) is int and recipe['version'] in (1, 2, 3, 4), 'unsupported layout schema/version')
    version = recipe['version']
    world_limit=8192 if version==4 else 1000
    node_limit=world_limit-32 if version==4 else 950
    require(type(recipe.get('seed')) is int and 0 <= recipe['seed'] <= 0xffffffff, 'invalid seed')
    require(recipe.get('units') == 'metres' and recipe.get('plane') == 'xz' and type(recipe.get('ground_y')) in (int, float) and recipe['ground_y'] == 0, 'expected flat native metre coordinates')
    require(type(recipe.get('pilot_radius')) in (int, float) and recipe['pilot_radius'] == PILOT_RADIUS, 'unexpected pilot clearance')
    bounds = recipe.get('bounds')
    require(rect(bounds) and min(bounds) >= -world_limit and max(bounds) <= world_limit, 'invalid world bounds')
    def inside(box):
        return bounds[0] <= box[0] <= box[2] <= bounds[2] and bounds[1] <= box[1] <= box[3] <= bounds[3]
    nodes = recipe.get('nodes')
    require(isinstance(nodes, list) and 2 <= len(nodes) <= (768 if version==4 else 81) and all(numbers(p, 2) for p in nodes), 'invalid road nodes or node budget')
    require(len({tuple(p) for p in nodes}) == len(nodes), 'duplicate road nodes')
    require(all(inside([x,z,x,z]) and abs(x)<=node_limit and abs(z)<=node_limit for x,z in nodes), 'node outside world')
    def node_index(i):
        return type(i) is int and 0 <= i < len(nodes)
    roads, plots = recipe.get('roads'), recipe.get('plots')
    require(isinstance(roads, list) and 1 <= len(roads) <= (1536 if version==4 else 144), 'invalid roads or road budget')
    require(isinstance(plots, list) and len(plots) <= (512 if version==4 else 64), 'invalid plots or plot budget')
    adjacency = [set() for _ in nodes]
    road_bounds = []
    for road in roads:
        require(isinstance(road, dict), 'invalid road')
        ends, width = road.get('nodes'), road.get('width')
        require(isinstance(ends, list) and len(ends) == 2 and all(node_index(i) for i in ends), 'invalid road endpoint')
        a, b = ends
        require(a != b and b not in adjacency[a], 'duplicate or zero-length road')
        require(number(width) and 16 <= width <= 60, 'road too narrow or wide')
        if 'class' in road:
            require(road['class'] in ('arterial', 'local'), 'invalid road class')
        pa, pb = nodes[a], nodes[b]
        require(pa[0] == pb[0] or pa[1] == pb[1], 'road must be axis aligned')
        box = [min(pa[0], pb[0])-width/2, min(pa[1], pb[1])-width/2, max(pa[0], pb[0])+width/2, max(pa[1], pb[1])+width/2]
        require(inside(box), 'road outside world')
        road_bounds.append(box)
        adjacency[a].add(b)
        adjacency[b].add(a)
    districts = recipe.get('districts', [])
    district_ids = {}
    if version >= 2 or 'districts' in recipe:
        require(isinstance(districts, list) and (1 <= len(districts) <= 64 if 'districts' in recipe else not districts), 'invalid districts')
        for district in districts:
            require(isinstance(district, dict) and type(district.get('id')) is int and 0 <= district['id'] <= 63 and district['id'] not in district_ids, 'invalid district id')
            require(district.get('role') in ROLES and sources(district.get('sources')), 'invalid district preferences')
            district_ids[district['id']] = district
    plot_bounds = []
    for i, plot in enumerate(plots):
        require(isinstance(plot, dict) and rect(plot.get('bounds')), 'invalid plot bounds')
        box = plot['bounds']
        require(box[2]-box[0] >= 8 and box[3]-box[1] >= 8, 'plot too small')
        require(all(abs(v) <= (world_limit-8 if version==4 else 980) for v in box) and inside(box), 'plot outside world')
        require(not any(overlap(box, other) for other in road_bounds+plot_bounds), 'plot overlaps a road or another plot')
        if version >= 2 or 'role' in plot:
            require(plot.get('role') in ROLES, 'invalid plot preferences')
        if version >= 2 or 'sources' in plot:
            require(sources(plot.get('sources')), 'invalid plot preferences')
        if version >= 2 or 'id' in plot:
            require(type(plot.get('id')) is int and plot['id'] == i, 'plot id must match array index')
        if 'district' in plot:
            require(type(plot['district']) is int and plot['district'] in district_ids, 'unknown plot district')
            district = district_ids[plot['district']]
            require(plot.get('role') == district['role'] and sources(plot.get('sources')) and
                    set(plot['sources']) == set(district['sources']), 'inconsistent plot district preferences')
        plot_bounds.append(box)
    spawn, objectives = recipe.get('spawn'), recipe.get('objectives')
    require(isinstance(spawn, dict) and isinstance(objectives, list) and 1 <= len(objectives) <= 8, 'invalid mission zones')
    mission = recipe.get('mission')
    if version >= 2:
        require(isinstance(mission, dict) and mission.get('kind') in KINDS and type(mission.get('extraction')) is bool, 'invalid mission plan')
        require(number(mission.get('hold_seconds')) and 1 <= mission['hold_seconds'] <= 30, 'invalid recon hold seconds')
    zone_nodes, zone_discs = set(), []
    def zone_clear(zone):
        require(isinstance(zone, dict) and node_index(zone.get('node')), 'invalid zone node')
        i, radius = zone['node'], zone.get('radius')
        require(number(radius) and radius >= PILOT_RADIUS, 'zone too small')
        x, z = nodes[i]
        box = [x-radius, z-radius, x+radius, z+radius]
        require(inside(box), 'zone outside world')
        require(not any(overlap(box, other) for other in plot_bounds), 'blocked mission zone')
        require(any(r[0] <= box[0] and r[1] <= box[1] and box[2] <= r[2] and box[3] <= r[3] for r in road_bounds), 'zone is not inside a road corridor')
        return i, x, z, radius
    strike_plots = set()
    for n, zone in enumerate([spawn]+objectives):
        i, x, z, radius = zone_clear(zone)
        require(i not in zone_nodes, 'zones share a node')
        zone_nodes.add(i)
        require(not any(math.hypot(x-px, z-pz) < radius+pr for px, pz, pr in zone_discs), 'mission zones overlap')
        zone_discs.append((x, z, radius))
        if n:
            expected = 'encounter' if version == 1 or mission['kind'] == 'eliminate' else mission['kind']
            require(zone.get('kind') == expected, 'objective kind does not match mission')
            if version >= 2 and mission['kind'] == 'strike':
                target = zone.get('plot')
                require(type(target) is int and 0 <= target < len(plots) and target not in strike_plots, 'invalid or duplicate strike plot')
                b = plot_bounds[target]
                dx, dz = max(b[0]-x, 0, x-b[2]), max(b[1]-z, 0, z-b[3])
                require(0 < dx <= (80 if version==4 else 40) and 0 < dz <= (80 if version==4 else 40), 'strike plot is not adjacent to its objective node')
                strike_plots.add(target)
    if version >= 2:
        if mission['extraction']:
            i, x, z, radius = zone_clear(recipe.get('extraction'))
            # Legacy extraction intentionally reuses the clear insertion point. It is
            # gated until objectives complete, preventing immediate completion.
            require(version==4 or i == spawn['node'], 'extraction must use the insertion node')
            require(not any(math.hypot(x-px, z-pz) < radius+pr for px, pz, pr in zone_discs[1:]), 'extraction overlaps an objective')
        else:
            require('extraction' not in recipe, 'disabled extraction has a zone')
    if version >= 3:
        terrain = recipe.get('terrain')
        require(isinstance(terrain, dict) and terrain.get('biome') in ('urban', 'grassland', 'desert', 'hills'), 'invalid terrain biome')
        require(number(terrain.get('amplitude')) and 0 <= terrain['amplitude'] <= 24, 'invalid terrain amplitude')
        regions = terrain.get('water')
        require(isinstance(regions, list) and len(regions) <= 8, 'invalid water regions')
        water_bounds = []
        for region in regions:
            require(isinstance(region, dict) and rect(region.get('bounds')) and inside(region['bounds']), 'invalid water bounds')
            require(number(region.get('level')) and -4 <= region['level'] <= 0 and number(region.get('depth')) and 2 <= region['depth'] <= 16, 'invalid water level or depth')
            require(not any(overlap(region['bounds'], other) for other in water_bounds), 'water regions overlap')
            water_bounds.append(region['bounds'])
        total = 0
        for plot in plots:
            require(type(plot.get('buildings')) is int and 0 <= plot['buildings'] <= 16, 'invalid building density')
            total += plot['buildings']
            if plot['buildings']:
                require(not any(overlap(plot['bounds'], body) for body in water_bounds), 'building parcel overlaps water')
        require(total <= (2048 if version==4 else 768), 'scenery budget exceeded')
        for zone in objectives:
            if mission['kind'] == 'strike':
                require(plots[zone['plot']]['buildings'] > 0, 'strike objective references an empty parcel')
        encounters = recipe.get('encounters')
        require(isinstance(encounters, dict) and type(encounters.get('ground_per_zone')) is int and 1 <= encounters['ground_per_zone'] <= 12 and encounters['ground_per_zone']*len(objectives) <= 64, 'invalid ground encounter budget')
        require(type(encounters.get('air_units')) is int and 0 <= encounters['air_units'] <= 8, 'invalid air encounter budget')
    if 'reinforcements' in recipe:
        r=recipe['reinforcements'];require(isinstance(r,dict),'invalid reinforcement policy')
        if r.get('mode')=='continuous':
            require(version==4 and 'per_zone' not in r,'continuous spawners require version four without finite tickets')
            require(number(r.get('interval')) and 5<=r['interval']<=120 and number(r.get('activation_range')) and 100<=r['activation_range']<=2000,'invalid spawner timing/range')
            require(type(r.get('ground_limit')) is int and 1<=r['ground_limit']<=64 and type(r.get('air_limit')) is int and 0<=r['air_limit']<=8 and type(r.get('kill_quota')) is int and 1<=r['kill_quota']<=512,'invalid spawner caps/kill quota')
            require(recipe['encounters']['ground_per_zone']*len(objectives)<=r['ground_limit'] and recipe['encounters']['air_units']<=r['air_limit'],'initial population exceeds reinforcement limits')
        else:
            require(r.get('mode','finite')=='finite' and type(r.get('per_zone')) is int and 0<=r['per_zone']<=12 and number(r.get('interval')) and 5<=r['interval']<=120,'invalid finite reinforcement policy')
    reached = _distances(adjacency, spawn['node'])
    require(len(reached) == len(nodes), 'road network is disconnected')
    if version==4:
        from tools.layout_learning.segments import validate_segments
        validate_segments(recipe)
    return {'connected_nodes': len(reached), 'clear_roads': len(roads), 'clear_plots': len(plots),
            'reachable_objectives': len(objectives), 'mission_kind': mission['kind'] if version >= 2 else 'eliminate',
            'extraction': bool(version >= 2 and mission['extraction']),
            'buildings': sum(p.get('buildings', 1) for p in plots),
            'water_regions': len(recipe.get('terrain', {}).get('water', []))}


def svg(recipe):
    validate(recipe)
    x0, z0, x1, z1 = recipe['bounds']
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{x0} {z0} {x1-x0} {z1-z0}">', f'<rect x="{x0}" y="{z0}" width="{x1-x0}" height="{z1-z0}" fill="#173c32"/>']
    for body in recipe.get('terrain', {}).get('water', []):
        a, b, c, d = body['bounds']
        parts.append(f'<rect x="{a}" y="{b}" width="{c-a}" height="{d-b}" fill="#287cbb"/>')
    for road in recipe['roads']:
        a, b = [recipe['nodes'][i] for i in road['nodes']]
        color = '#9daab0' if road.get('class') == 'arterial' else '#657980'
        parts.append(f'<path d="M {a[0]} {a[1]} L {b[0]} {b[1]}" stroke="{color}" stroke-width="{road["width"]}" stroke-linecap="square"/>')
    colors = {'industrial': '#bba378', 'storage': '#c68f64', 'urban': '#9a8ead'}
    for plot in recipe['plots']:
        a, b, c, d = plot['bounds']
        if plot.get('buildings', 1) == 0:
            continue
        count = plot.get('buildings', 1);cols = math.ceil(math.sqrt(count));rows = math.ceil(count/cols)
        for cell in range(count):
            x, z = a+(cell % cols)*(c-a)/cols, b+(cell//cols)*(d-b)/rows
            gap = min(2, (c-a)/cols/6, (d-b)/rows/6)
            parts.append(f'<rect x="{x+gap}" y="{z+gap}" width="{(c-a)/cols-2*gap}" height="{(d-b)/rows-2*gap}" fill="{colors[plot.get("role", "industrial")]}"/>')
    for zone, color in [(recipe['spawn'], '#7aff85')]+[(z, {'encounter': '#ff655e', 'strike': '#ffbc42', 'recon': '#65baff'}[z['kind']]) for z in recipe['objectives']]:
        x, z = recipe['nodes'][zone['node']]
        parts.append(f'<circle cx="{x}" cy="{z}" r="{zone["radius"]}" fill="{color}"/>')
        if zone.get('kind') == 'strike' and 'plot' in zone:
            a, b, c, d = recipe['plots'][zone['plot']]['bounds']
            parts.append(f'<rect x="{a}" y="{b}" width="{c-a}" height="{d-b}" fill="none" stroke="#ffbc42" stroke-width="3"/>')
    if recipe.get('mission', {}).get('extraction'):
        zone = recipe['extraction'];x, z = recipe['nodes'][zone['node']]
        parts.append(f'<circle cx="{x}" cy="{z}" r="{zone["radius"]+4}" fill="none" stroke="#65ffff" stroke-width="3"/>')
    return '\n'.join(parts+['</svg>'])+'\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--cells', type=int, default=4)
    parser.add_argument('--spacing', type=int, default=120)
    parser.add_argument('--objectives', type=int, default=3)
    parser.add_argument('--mission-kind', choices=KINDS, default='eliminate')
    parser.add_argument('--extract', action='store_true', help='return to insertion after completing objectives')
    parser.add_argument('--hold-seconds', type=float, default=4)
    parser.add_argument('--terrain', choices=('auto', 'urban', 'grassland', 'desert', 'hills'), default='auto')
    parser.add_argument('--water', choices=('auto', 'none', 'lake', 'canal'), default='auto')
    parser.add_argument('--density', type=int, default=9)
    parser.add_argument('--ground-per-zone', type=int, default=6)
    parser.add_argument('--air-units', type=int, default=3)
    parser.add_argument('--format-version', type=int, choices=(2, 3), default=3)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--svg', type=Path)
    parser.add_argument('--validate', type=Path)
    args = parser.parse_args()
    try:
        recipe = json.loads(args.validate.read_text()) if args.validate else generate(args.seed, args.cells, args.spacing, args.objectives, args.mission_kind, args.extract, args.hold_seconds, args.terrain, args.water, args.density, args.ground_per_zone, args.air_units, args.format_version)
        report = validate(recipe)
        if args.svg:
            args.svg.write_text(svg(recipe))
        if args.output:
            args.output.write_text(json.dumps(recipe, indent=2, sort_keys=True)+'\n')
        elif not args.validate:
            print(json.dumps(recipe, indent=2, sort_keys=True))
        if args.validate:
            print(json.dumps(report, sort_keys=True))
    except (ValueError, OSError, TypeError, KeyError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
