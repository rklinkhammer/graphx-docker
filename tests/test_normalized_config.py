#!/usr/bin/env python3
"""Normalized v2 determinism, atomic cutover and credential boundary."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    graphx, root = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    source = root / 'examples/sample-pipeline/graphx.yml'
    def run(*args, **kwargs):
        return subprocess.run([graphx, *map(str, args)], capture_output=True, text=True, timeout=15, **kwargs)
    first = run('config', 'normalize', source)
    assert first.returncode == 0, first.stderr
    second = run('config', 'normalize', source, env={**os.environ, 'GRAPHX_OVERRIDES': 'version=2', 'GRAPHX_CONFIG': '/nonexistent'})
    assert first.stdout == second.stdout
    value = json.loads(first.stdout)
    assert value['contract_version'] == 2 and value['graph_version'] == 3
    assert value['platform']['history']['enabled'] is True
    assert not {'graph', 'deployment', 'observability'} & value.keys()
    fixture = json.loads((root / 'tests/fixtures/normalized/sample-pipeline.json').read_text())
    assert value == fixture, 'normalized snapshot changed; review then regenerate from C++'
    for args in [('config', 'normalize', source, '--set', 'version=3'), ('run', source), ('compile', source), ('infra', 'create', source, '--dry-run')]:
        result = run(*args)
        assert result.returncode and not result.stdout
    with tempfile.TemporaryDirectory() as directory:
        folder = Path(directory)
        old = folder / 'graphx.yml'
        old.write_text('version: 2\ngraph: {}\n')
        result = run('validate', old)
        assert result.returncode and 'E_VERSION' in result.stderr
        old.unlink()
        old.symlink_to(source)
        result = run('validate', old)
        assert result.returncode and 'E_INPUT' in result.stderr
        old.unlink()
        # No credential material is opened by normalization, even a nonexistent file reference.
        text = (root / 'examples/sdr-node/simulated/graphx.yml').read_text()
        import re
        text = re.sub(r'^catalog:.*$', 'catalog: ' + os.path.relpath(root / 'config/catalog/lock.json', folder), text, flags=re.M)
        old.write_text(text)
        result = run('config', 'normalize', old)
        assert result.returncode == 0, result.stderr
        assert 'file:' not in result.stdout and 'PRIVATE KEY' not in result.stdout
    from test_catalog_boundary import main as catalog_boundary
    catalog_boundary(graphx, root)
    print('normalized v2 snapshot, deterministic bytes, no ambient overrides, v2 and execution rejection, secret references passed')


if __name__ == '__main__':
    main()
