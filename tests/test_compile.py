#!/usr/bin/env python3
"""Deterministic compiler matrix, goldens, publication boundaries and no runtime calls."""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

cli, root = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
design = root / 'design/graph-generation'


def digest(data):
    return hashlib.sha256(data).hexdigest()


def files(path):
    return {str(p.relative_to(path)): p.read_bytes() for p in sorted(path.rglob('*')) if p.is_file()}


def compile_to(source, output, catalog=None, target='native-linux', **options):
    command = [str(cli), 'compile', str(source), '--output', str(output),
        '--source-root', str(options.pop('source_root', source.parent)),
        '--credential-root', str(options.pop('credential_root', output.parent / 'credentials')),
        '--catalog-root', str(catalog or root / 'config/catalog'), '--target', target]
    result = subprocess.run(command + options.pop('extra', []), capture_output=True, text=True,
                            timeout=15, env=options.pop('env', None))
    assert not options, options
    return result


def verify(output):
    content = files(output)
    manifest = json.loads(content['compile-manifest.json'])
    assert isinstance(manifest['execution_available'], bool)
    assert manifest['artifact_identities_verified'] is False
    assert set(content) == {p['path'] for p in manifest['files']} | {'compile-manifest.json'}
    assert all(digest(content[p['path']]) == p['sha256'] for p in manifest['files'])
    resolved = json.loads(content['resolved.json'])
    for node in resolved['nodes']:
        expected = (json.dumps(node, indent=2, sort_keys=True, ensure_ascii=False) + '\n').encode()
        assert content['nodes/' + node['node_id'] + '.json'] == expected
    assert all(b'\r' not in data and data.endswith(b'\n') for data in content.values())
    assert set(json.loads(content['substitutions.json'])['values']) == {'GX_OUTPUT', 'GX_STATE', 'GX_CREDENTIALS', 'GX_RELEASE', 'GX_OWNER'}
    execution = json.loads(content['execution-plan.json'])
    assert execution['executable'] == manifest['execution_available']
    assert execution['mode'] == ('compose' if 'compose.yaml' in content else 'native')
    if any(a['kind'] == 'external' for a in resolved['network']['attachments']):
        assert execution['executable'] is False
    else:
        assert execution['executable'] is True
    operations = [s['operation'] for s in execution['stages']]
    assert operations.index('start-applications-held') < operations.index('await-local-listeners') < operations.index('release-connectors')
    if any(n['execution']['kind'] == 'namespace' for n in resolved['nodes']):
        assert operations.index('prepare-owned-namespaces') < operations.index('start-applications-held')
    if 'compose.yaml' in content:
        compose = json.loads(content['compose.yaml'])
        for name, service in compose['services'].items():
            assert '@sha256:' in service['image'] and 'build' not in service and not service.get('privileged')
            assert service['cap_drop'] == ['ALL'] and service['read_only'] is True
            for mount in service.get('volumes', []):
                if mount.startswith('./'):
                    assert mount.split(':')[0][2:] in content, mount
            if name not in {'platform', 'prometheus', 'grafana'}:
                assert 'mg-console' not in service.get('networks', [])
                assert '--release-token' in service['command'] and '--node' in service['command']
                assert 'healthcheck' not in service, 'P2 readiness uses stdout, not an unsupported CLI probe'
                node = next(n for n in resolved['nodes'] if n['node_id'] == name)
                if node['telemetry']['credential'] is not None:
                    assert service['environment']['GRAPHX_TELEMETRY_SHARED_SECRET_FILE'] == '/run/secrets/' + node['telemetry']['credential'] + '/hmac'
                refs = json.loads(content['credentials.json'])['allowed_consumers']
                for mount in service['volumes']:
                    if mount.startswith('${GX_CREDENTIALS}/'):
                        ref = mount.split('/')[1].split(':')[0]
                        assert name in refs[ref], (name, ref)
    if 'ovs-plan.json' in content:
        assert json.loads(content['ovs-plan.json'])['resource_intent'] == resolved['network']
    if 'scenario-plan.json' in content:
        assert json.loads(content['scenario-plan.json'])['implicit_start'] is False
    assert all(not p.name.startswith('.graphx-compile-') for p in output.parent.iterdir())
    return content


with tempfile.TemporaryDirectory(prefix='graphx-p3-') as temporary:
    temp = Path(temporary).resolve()
    # A compiler must not invoke engine, network, guest or credential tools.
    shims = temp / 'shims'
    shims.mkdir()
    for name in ['docker', 'qemu-system-x86_64', 'ip', 'nft', 'ovs-vsctl', 'openssl', 'sudo', 'limactl']:
        tool = shims / name
        tool.write_text('#!/bin/sh\necho unexpected-runtime-call >&2\nexit 99\n')
        tool.chmod(0o755)
    environment = {**os.environ, 'PATH': str(shims), 'GRAPHX_OVERRIDES': 'invalid', 'GX_OWNER': 'ignored', 'GX_STATE': '/ignored'}
    positive = negative = unsupported = compose_checked = 0
    for case in json.loads((design / 'inventory.json').read_text())['cases']:
        expected = design / case['path'] / 'expected'
        targets = [case['target']] if case['kind'] == 'negative' else sorted(p.name for p in expected.iterdir() if p.is_dir())
        for target in targets:
            output = temp / (case['id'] + '-' + target)
            result = compile_to(design / case['input'], output, design / 'catalog', target, env=environment)
            diagnostic = design / case['diagnostic'] if case['kind'] == 'negative' else expected / target / 'diagnostic.json'
            if diagnostic.exists():
                want = json.loads(diagnostic.read_text())
                assert result.returncode and want['code'] in result.stderr and not output.exists(), (case['id'], result.stderr)
                if case['kind'] == 'negative': negative += 1
                else: unsupported += 1
                continue
            assert result.returncode == 0, (case['id'], target, result.stderr)
            original = verify(output)
            if os.environ.get('GRAPHX_COMPOSE_VALIDATE') == '1' and 'compose.yaml' in original:
                compose_env = {**os.environ, 'GX_OUTPUT': str(output), 'GX_STATE': str(temp / 'state'),
                    'GX_CREDENTIALS': str(temp / 'credentials'), 'GX_RELEASE': str(temp / 'release'), 'GX_OWNER': 'a' * 64}
                check = subprocess.run(['docker', 'compose', '--project-directory', str(output),
                    '-f', str(output / 'compose.yaml'), 'config', '--quiet'],
                    capture_output=True, text=True, timeout=15, env=compose_env)
                assert check.returncode == 0, (case['id'], check.stderr)
                compose_checked += 1
            repeat = temp / (output.name + '-repeat')
            again = compile_to(design / case['input'], repeat, design / 'catalog', target)
            assert again.returncode == 0 and verify(repeat) == original, (case['id'], again.stderr)
            positive += 1
    assert (positive, negative, unsupported) == (63, 15, 33)
    if compose_checked:
        print(f'{compose_checked} read-only Docker Compose configurations passed')
    print('63 target packages repeat byte-for-byte; 15 negative cases; 33 unsupported targets; no runtime tools')

    # Maintained production goldens cover every serializer family, not review hashes.
    goldens = {'sample-pipeline': 'sample-pipeline', 'shared-memory': 'shared-memory', 'qemu-tap': 'qemu-node/tap',
               'sdr-simulated': 'sdr-node/simulated', 'static-route-policy': 'static-route-policy', 'four-radio-vita': 'four-radio-vita'}
    for label, example in goldens.items():
        output = temp / ('golden-' + label)
        result = compile_to(root / f'examples/{example}/graphx.yml', output)
        assert result.returncode == 0, result.stderr
        actual = verify(output)
        golden = root / 'tests/fixtures/compiled' / label
        if os.environ.get('GRAPHX_UPDATE_COMPILE_GOLDENS') == '1':
            golden.mkdir(parents=True, exist_ok=True)
            for name, data in actual.items():
                path = golden / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
        assert actual == files(golden), f'golden changed: {label}; inspect before regenerating'

    # Relocated identical inputs and shuffled authored mappings have identical bytes.
    source = root / 'examples/sample-pipeline/graphx.yml'
    baseline = files(temp / 'golden-sample-pipeline')
    relocated = temp / 'relocated'
    shutil.copytree(root / 'config/catalog', relocated / 'config/catalog')
    (relocated / 'examples/sample-pipeline').mkdir(parents=True)
    copied = relocated / 'examples/sample-pipeline/graphx.yml'
    copied.write_bytes(source.read_bytes())
    target = temp / 'relocation-output'
    result = compile_to(copied, target, relocated / 'config/catalog')
    assert result.returncode == 0 and files(target) == baseline, result.stderr
    # Equivalent authored JSON also checks YAML/JSON normalization without a Python parser.
    authored = {'version': 3, 'catalog': '../../config/catalog/lock.json',
        'graph': {'id': 'sample-pipeline'},
        'nodes': {name: {'type': kind, 'execution': {'kind': 'container'}, 'parameters': {'max_messages': 0}} for name, kind in
                  [('generator', 'sample.source'), ('sink', 'sample.sink'), ('transform', 'sample.transform')]},
        'connections': {'samples': {'from': 'generator.samples', 'to': 'transform.samples', 'transport': 'tcp'},
                        'transformed': {'from': 'transform.transformed', 'to': 'sink.transformed', 'transport': 'tcp'}}}
    def reverse_maps(value):
        if isinstance(value, dict):
            return {k: reverse_maps(v) for k, v in reversed(list(value.items()))}
        if isinstance(value, list):
            return [reverse_maps(v) for v in value]
        return value
    copied.write_text(json.dumps(reverse_maps(authored)))
    reordered = temp / 'reordered'
    result = compile_to(copied, reordered, relocated / 'config/catalog')
    assert result.returncode == 0 and files(reordered) == baseline, result.stderr

    # All existing destinations are immutable, including correctly hashed output.
    for destination in [temp / 'golden-sample-pipeline', temp / 'empty', temp / 'foreign', temp / 'link']:
        if not destination.exists():
            if destination.name == 'link': destination.symlink_to(temp / 'foreign')
            else: destination.mkdir()
        if destination.name == 'foreign': (destination / 'unowned').write_text('preserve')
        before = files(destination)
        result = compile_to(source, destination)
        assert result.returncode and 'E_OUTPUT_OWNERSHIP' in result.stderr and files(destination) == before
    assert compile_to(source, temp / 'replace', extra=['--replace']).returncode != 0
    for root_name in ['source_root', 'credential_root']:
        destination = temp / ('overlap-' + root_name)
        result = compile_to(source, destination, **{root_name: temp})
        assert result.returncode and 'E_OUTPUT_ROOT' in result.stderr and not destination.exists()
    parent = temp / 'parent-link'
    parent.symlink_to(temp / 'foreign', target_is_directory=True)
    result = compile_to(source, parent / 'child')
    assert result.returncode and 'E_OUTPUT_PATH' in result.stderr and not (temp / 'foreign/child').exists()
    copied.unlink()
    copied.symlink_to(source)
    assert compile_to(copied, temp / 'input-link', relocated / 'config/catalog').returncode != 0
    copied.unlink()
    copied.write_bytes(source.read_bytes())
    # Racing publishers cannot overwrite the winning complete directory.
    race = temp / 'race'
    command = [str(cli), 'compile', str(source), '--output', str(race), '--source-root', str(source.parent),
               '--credential-root', str(temp / 'credentials')]
    racers = [subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE) for _ in range(2)]
    results = [(p.communicate(timeout=15), p.returncode) for p in racers]
    assert sorted(code for _, code in results) == [0, 2], results
    verify(race)
    # Mutated catalog bytes fail their pin before publication; repinned hostile templates fail closed.
    catalog = relocated / 'config/catalog'
    template = catalog / 'templates.json'
    template.write_text(template.read_text() + ' ')
    result = compile_to(copied, temp / 'tampered', catalog)
    assert result.returncode and 'E_CATALOG_DIGEST' in result.stderr
    value = json.loads(template.read_text())
    value['node-v1']['security']['cap_drop'] = []
    template.write_text(json.dumps(value))
    lock = json.loads((catalog / 'lock.json').read_text())
    next(f for f in lock['files'] if f['path'] == 'templates.json')['sha256'] = digest(template.read_bytes())
    (catalog / 'lock.json').write_text(json.dumps(lock))
    result = compile_to(copied, temp / 'hostile-template', catalog)
    assert result.returncode and 'E_COMPILE_TEMPLATE' in result.stderr
    def repin(name):
        data = json.loads((catalog / 'lock.json').read_text())
        next(f for f in data['files'] if f['path'] == name)['sha256'] = digest((catalog / name).read_bytes())
        (catalog / 'lock.json').write_text(json.dumps(data))
    template.write_bytes((root / 'config/catalog/templates.json').read_bytes())
    repin('templates.json')
    image_file = catalog / 'types/sample.source.json'
    image_type = json.loads(image_file.read_text())
    image_type['image'] = 'graphx/runtime:latest'
    image_file.write_text(json.dumps(image_type))
    repin('types/sample.source.json')
    result = compile_to(copied, temp / 'mutable-image', catalog)
    assert result.returncode and not (temp / 'mutable-image').exists(), result.stderr
    image_file.write_bytes((root / 'config/catalog/types/sample.source.json').read_bytes())
    repin('types/sample.source.json')
    qemu = relocated / 'examples/qemu-node/tap/graphx.yml'
    qemu.parent.mkdir(parents=True)
    qemu.write_bytes((root / 'examples/qemu-node/tap/graphx.yml').read_bytes())
    sources = json.loads((catalog / 'sources.json').read_text())
    sources['entries']['guest-source-echo-x86']['relative_path'] = '../escape'
    (catalog / 'sources.json').write_text(json.dumps(sources))
    repin('sources.json')
    result = compile_to(qemu, temp / 'source-escape', catalog)
    assert result.returncode and 'E_COMPILE_SOURCE' in result.stderr and not (temp / 'source-escape').exists()
    # Credential files are never read, even when the explicit protected root exists.
    credential_root = temp / 'private-credentials'
    credential_root.mkdir()
    sentinel = 'private-credential-sentinel-never-rendered'
    (credential_root / 'token').write_text(sentinel)
    clean = temp / 'secret-boundary'
    result = compile_to(source, clean, credential_root=credential_root)
    assert result.returncode == 0 and all(sentinel.encode() not in data for data in files(clean).values())
    print('Goldens, relocation/order/env determinism, root/symlink/tamper/replacement boundaries passed')
