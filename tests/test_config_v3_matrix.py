#!/usr/bin/env python3
"""Execute the accepted design inputs against the authoritative P1 loader."""
import json
from pathlib import Path
import subprocess
import sys


def main():
    graphx, root = Path(sys.argv[1]), Path(sys.argv[2])
    design = root / 'design/graph-generation'
    cases = json.loads((design / 'inventory.json').read_text())['cases']
    positive = negative = unsupported = 0
    for case in cases:
        expected = design / case['path'] / 'expected'
        targets = [case['target']] if case['kind'] == 'negative' else [p.name for p in expected.iterdir() if p.is_dir()]
        for target in sorted(targets):
            result = subprocess.run([graphx, 'config', 'normalize', design / case['input'], '--target', target,
                                     '--catalog-root', design / 'catalog'], capture_output=True, text=True, timeout=15)
            diagnostic = design / case['diagnostic'] if case['kind'] == 'negative' else expected / target / 'diagnostic.json'
            if diagnostic.exists():
                want = json.loads(diagnostic.read_text())
                assert result.returncode and not result.stdout, (case['id'], target, result.stdout)
                assert want['code'] in result.stderr and want['path'] in result.stderr, (case['id'], target, want, result.stderr)
                if case['kind'] == 'negative': negative += 1
                else: unsupported += 1
                continue
            assert result.returncode == 0, (case['id'], target, result.stderr)
            value = json.loads(result.stdout)
            assert value['contract_version'] == 2 and value['graph_version'] == 3
            assert value['target'] == target
            assert [n['node_id'] for n in value['nodes']] == sorted(n['node_id'] for n in value['nodes'])
            assert [c['id'] for c in value['connections']] == sorted(c['id'] for c in value['connections'])
            assert value['platform']['history']['enabled'] is True
            for connection in value['connections']:
                for side, role in [('from', 'connect'), ('to', 'listen')]:
                    ep = connection[side]
                    node = next(n for n in value['nodes'] if n['node_id'] == ep['node'])
                    binding = next(b for b in node['bindings'][ep['port']] if b['connection'] == connection['id'])
                    assert binding['role'] == role and binding['settings'] == connection['settings']
            positive += 1
    assert (positive, negative, unsupported) == (63, 15, 33), (positive, negative, unsupported)
    print(f'{positive} supported target normalizations; {negative} exact negative diagnostics; {unsupported} unsupported targets')


if __name__ == '__main__':
    main()
