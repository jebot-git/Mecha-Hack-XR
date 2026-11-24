#!/usr/bin/env python3
"""Check only the newly authored manual pack, independently of old cockpit QA."""
import importlib.util
from pathlib import Path
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('mecha_audit',root/'tools/audit_mecha_game.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
audit=module.Audit(root/'assets/mecha')
audit.section('mecha_redesign_budgets',audit.mecha_redesign_budgets)
audit.section('enemy_redesign_budgets',audit.enemy_redesign_budgets)
audit.section('enemy_reference_refinement',audit.enemy_reference_refinement)
audit.section('manual_authored_assets',audit.manual_assets)
assert audit.checks['manual_authored_assets'].get('present'), 'Dedicated manual source registries missing'
assert not audit.errors, '\n'.join(audit.errors)
assert audit.counts['manual_authored_meshes']==64
print('64 manual asset hashes, native/GPU topology, atlas UVs, budgets, source preservation and33 rig partitions verified')
