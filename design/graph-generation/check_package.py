#!/usr/bin/env python3
"""DESIGN ONLY: static artifact checks; never compile or execute a GraphX graph."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import yaml
import jsonschema

P = Path(__file__).resolve().parent
STATUS = {'run-current', 'static-design', 'planned-portable', 'planned-docker',
          'planned-privileged', 'planned-guest-boot'}

class StrictLoader(yaml.SafeLoader):
    pass

def mapping(loader, node, deep=False):
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        assert key not in result, f'duplicate YAML key {key}'
        result[key] = loader.construct_object(value_node, deep=deep)
    return result

StrictLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, mapping)

def unique_json(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, f'duplicate JSON key {key}'
        result[key] = value
    return result

def read(p):
    if p.suffix == '.json':
        return json.loads(p.read_text(), object_pairs_hook=unique_json)
    return yaml.load(p.read_text(), Loader=StrictLoader)

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def require_path(base, value):
    path = base / value
    assert path.exists(), f'missing path: {path}'
    assert not path.is_symlink(), f'symlink: {path}'
    return path

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compose', action='store_true', help='read-only docker compose config; no engine access')
    args = parser.parse_args()
    inv = read(P / 'inventory.json')
    ids = {c['id'] for c in inv['cases']}
    expected = {f'S{i:02}' for i in range(1, 16)} | {f'V{i:02}' for i in range(1, 7)} | {f'T{i:02}' for i in range(1, 4)} | {f'N{i:02}' for i in range(1, 16)}
    assert ids == expected and len(inv['cases']) == len(ids)
    parsed = 0
    for p in sorted(P.rglob('*')):
        assert not p.is_symlink()
        if p.suffix in {'.json', '.yaml', '.yml'}:
            read(p)
            parsed += 1
    schema = read(P.parents[1] / 'config/schema/graphx.schema.json')
    normalized = read(P.parents[1] / 'config/schema/normalized-graph.schema.json')
    type_schema = read(P.parents[1] / 'config/schema/node-type.schema.json')
    for s in [schema, normalized, type_schema]:
        jsonschema.Draft202012Validator.check_schema(s)
    types = {p.stem: read(p) for p in (P / 'catalog/types').glob('*.json')}
    wires = read(P / 'catalog/wire-schemas.json')
    for t in types.values():
        jsonschema.validate(t, type_schema)
        for p in t['ports'].values():
            assert p['schema'] in wires
    lockpath = P / 'catalog/lock.json'
    for f in read(lockpath)['files']:
        assert sha(require_path(lockpath.parent, f['path'])) == f['sha256']
    sources = read(P / 'catalog/sources.json')['entries']
    for guest in (P / 'catalog/guests').glob('*.json'):
        assert read(guest)['source_ref'] in sources
    represented = {P / f['path'] for f in inv['shared_files']}
    for p in represented:
        assert p.is_file(), p
    positive = negatives = sets = composes = diagnostics = 0
    for c in inv['cases']:
        assert c['status'] in STATUS
        root = require_path(P, c['path'])
        represented.update(p for p in root.rglob('*') if p.is_file())
        require_path(root, 'README.md')
        g = read(require_path(P, c['input']))
        assert g['version'] == 3
        if c['kind'] == 'negative':
            d = read(require_path(P, c['diagnostic']))
            assert set(d) == {'code', 'path', 'message', 'severity', 'phase', 'no_artifacts'}
            assert d['code'].startswith('E_') and d['severity'] == 'error' and d['no_artifacts'] is True
            assert not (root / 'expected').exists()
            negatives += 1
            continue
        positive += 1
        jsonschema.validate(g, schema)
        assert (root / g['catalog']).resolve() == lockpath
        binding_counts = {}
        for n, node in g['nodes'].items():
            t = types[node['type']]
            assert node['execution']['kind'] in t['execution'], (c['id'], n)
            for k, v in node.get('parameters', {}).items():
                assert k in t['parameters']
                assert t['parameters'][k]['minimum'] <= v <= t['parameters'][k]['maximum']
            for ref in node.get('credentials', {}).values():
                assert ref in g['credentials']
        for cid, con in g['connections'].items():
            pp = []
            for end, direction in [('from', 'output'), ('to', 'input')]:
                n, p = con[end].split('.')
                pp.append(types[g['nodes'][n]['type']]['ports'][p])
                assert pp[-1]['direction'] == direction
                assert con['transport'] in pp[-1]['transports']
                binding_counts[n, p] = binding_counts.get((n, p), 0) + 1
            assert pp[0]['schema'] == pp[1]['schema'] and pp[0]['encoding'] == pp[1]['encoding']
        for n, node in g['nodes'].items():
            for p, spec in types[node['type']]['ports'].items():
                assert spec['min_connections'] <= binding_counts.get((n, p), 0) <= spec['max_connections'], (c['id'], n, p)
        ci = read(require_path(P, c['expected_inventory']))
        assert ci['case_id'] == c['id']
        assert set(ci['capabilities']) == {'native-linux', 'native-macos', 'orbstack', 'lima'}
        supported = {k for k, v in ci['capabilities'].items() if v['cell'] == 'supported placement'}
        assert supported == {a['target'] for a in ci['artifact_sets']}
        for target, cap in ci['capabilities'].items():
            assert cap['cell'] in {'supported placement', 'not applicable', 'unsupported'}
            assert cap['status'] == 'static-design' and cap['rule']
            if cap['cell'] == 'unsupported':
                d = read(require_path(root, cap['diagnostic']))
                assert d['code'].startswith('E_') and d['no_artifacts'] is True
                diagnostics += 1
        for a in ci['artifact_sets']:
            sets += 1
            ar = root / 'expected' / a['target']
            declared = {root / f['path'] for f in a['files']}
            assert declared == {p for p in ar.rglob('*') if p.is_file()}, ar
            for f in a['files']:
                assert f['status'] == 'static-design' and f['completeness'] == 'complete-illustrative'
            r = read(ar / 'resolved.json')
            assert r['contract_version'] == 2
            assert r['catalog_digest'] == sha(lockpath)
            assert r['input_digest'] == hashlib.sha256(json.dumps(g, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
            assert [n['node_id'] for n in r['nodes']] == sorted(g['nodes'])
            assert [n['id'] for n in r['connections']] == sorted(g['connections'])
            for n in r['nodes']:
                assert read(ar / 'nodes' / (n['node_id'] + '.json')) == n
            assert read(ar / 'platform.json') == r['platform']
            assert r['platform']['history']['enabled'] is True
            assert r['platform']['history']['max_database_bytes'] > 0
            credentials = read(ar / 'credentials.json')['entries']
            assert sorted(credentials) == r['credential_references']
            net = r['network']
            if (ar / 'ovs-plan.json').exists():
                assert read(ar / 'ovs-plan.json')['resource_intent'] == net
            atts = {a['id']: a for a in net.get('attachments', [])}
            assert len(atts) == len(net.get('attachments', []))
            for con in r['connections']:
                for ref in con['attachments'].values():
                    assert ref in atts, (c['id'], ref)
            for capture in net.get('captures', []):
                assert capture['attachment'] in atts
            for att in atts.values():
                for k in ['interface', 'peer']:
                    if k in att:
                        assert len(att[k]) <= 15
            subs = read(ar / 'substitutions.json')['values']
            assert set(subs) == {'GX_OUTPUT', 'GX_STATE', 'GX_CREDENTIALS', 'GX_RELEASE', 'GX_OWNER'}
            for f in declared:
                scan = f.read_text() if f.name != 'substitutions.json' else json.dumps(read(f)['values'])
                for token in re.findall(r'\$\{([^}]+)\}', scan):
                    assert token in subs, (f, token)
                assert '-----BEGIN PRIVATE KEY-----' not in f.read_text()
            for name, spec in subs.items():
                assert spec['type'] in {'absolute-directory', 'identity-token'}
                if spec['type'] == 'absolute-directory':
                    assert spec['review_value'].startswith('/') and '..' not in Path(spec['review_value']).parts
            counts = a['counts']
            natives = read(ar / 'native-plan.json')['processes'] if (ar / 'native-plan.json').exists() else []
            assert len(natives) == counts['native_application_processes'] + counts['native_platform_processes']
            guests = read(ar / 'qemu-plan.json')['guests'] if (ar / 'qemu-plan.json').exists() else []
            assert len(guests) == counts['qemu_processes']
            assert counts['capture_processes'] == len(net.get('captures', []))
            assert counts['handoff_processes'] == int(bool(net.get('captures')))
            if (ar / 'compose.yaml').exists():
                comp = read(ar / 'compose.yaml')
                assert len(comp['services']) == counts['application_services'] + counts['platform_services'] + counts['auxiliary_services']
                assert not any(n.get('driver') in {'macvlan', 'ipvlan'} for n in comp['networks'].values())
                for service in comp['services'].values():
                    assert '@sha256:' in service['image']
                    assert service['read_only'] and service['cap_drop'] == ['ALL']
                if args.compose:
                    text = (ar / 'compose.yaml').read_text()
                    for name, spec in subs.items():
                        text = text.replace('${' + name + '}', spec['review_value'])
                    with tempfile.TemporaryDirectory(prefix='graphx-design-compose-') as td:
                        cp = Path(td) / 'compose.yaml'
                        cp.write_text(text)
                        result = subprocess.run(['docker', 'compose', '-f', str(cp), 'config', '--quiet'], capture_output=True, text=True, timeout=30)
                        assert result.returncode == 0, f'{ar}: {result.stderr}'
                    composes += 1
            else:
                assert counts['application_services'] + counts['platform_services'] + counts['auxiliary_services'] == 0
            manifest = read(ar / 'compile-manifest.json')
            assert {f['path'] for f in manifest['files']} == {str(p.relative_to(ar)) for p in declared if p.name != 'compile-manifest.json'}
            for f in manifest['files']:
                assert sha(ar / f['path']) == f['sha256'], (ar, f['path'])
    assert represented == {p for p in P.rglob('*') if p.is_file()}, 'unindexed package file'
    links = 0
    for p in P.rglob('*.md'):
        for link in re.findall(r'\]\(([^)]+)\)', p.read_text()):
            if re.match(r'^[a-z]+://', link) or link.startswith('#'):
                continue
            path = link.split('#')[0]
            assert (p.parent / path).exists(), (p, link)
            links += 1
    architecture = (P / 'architecture.md').read_text()
    for i in range(1, 12):
        assert len(re.findall(r'^\| I-' + f'{i:02}' + r' \|', architecture, re.M)) == 1
    for cid in ids:
        assert len(re.findall(r'^\| ' + cid + r' \|', architecture, re.M)) == 1
    print(json.dumps({'status': 'static-design', 'result': 'pass', 'positive_cases': positive,
                      'negative_inputs': negatives, 'target_sets': sets,
                      'unsupported_target_diagnostics': diagnostics, 'parsed_yaml_json': parsed,
                      'relative_links': links, 'compose_config_checks': composes,
                      'limitations': ['no production compiler or negative diagnostic execution',
                                      'normalized schema checks outer shape only',
                                      'no runtime, isolation, rendering determinism or guest boot evidence']}, indent=2))

if __name__ == '__main__':
    main()
