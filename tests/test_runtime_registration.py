#!/usr/bin/env python3
"""Portable trusted execution registration and fail-closed publication tests."""
import concurrent.futures
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main():
    graphx, root = map(Path, sys.argv[1:])
    config = root / 'examples/sdr-node/two-source/graphx.yaml'
    with tempfile.TemporaryDirectory(prefix='graphx-runtime-') as raw:
        directory = Path(raw)
        manifest = directory / 'identities.json'
        original = {'version': 2, 'graph_id': 'two-source-sdr', 'instance_id': 'lab-a',
                    'nodes': [{'id': node, 'secret_file': node + '.secret'} for node in
                              ['sdr-east', 'sdr-west', 'processor-east', 'processor-west']]}
        manifest.write_text(json.dumps(original))
        manifest.chmod(0o600)

        def run(action, *args):
            return subprocess.run([str(graphx), 'runtime', action, str(config),
                                   '--identity-file', str(manifest), '--node', 'sdr-east', *args],
                                  capture_output=True, text=True, timeout=10,
                                  env={**os.environ, 'GRAPHX_OVERRIDES': ''})

        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            results = list(pool.map(lambda _: run('activate'), range(2)))
        assert sum(result.returncode == 0 for result in results) == 1
        execution = next(result.stdout.strip() for result in results if result.returncode == 0)
        assert re.fullmatch('[0-9a-f]{32}', execution) and execution != '0' * 32
        document = json.loads(manifest.read_text())
        assert document['version'] == 2
        before = manifest.read_bytes()
        assert run('retire', '--execution-id', 'b' * 32).returncode != 0
        assert manifest.read_bytes() == before
        assert run('retire', '--execution-id', execution).returncode == 0
        fresh = run('activate')
        assert fresh.returncode == 0 and fresh.stdout.strip() != execution
        before = manifest.read_bytes()
        assert run('retire', '--execution-id', execution).returncode != 0
        assert manifest.read_bytes() == before
        assert run('retire', '--execution-id', fresh.stdout.strip()).returncode == 0
        before = manifest.read_bytes()
        hardlink = directory / 'hardlink.json'
        os.link(manifest, hardlink)
        assert run('activate').returncode != 0
        assert manifest.read_bytes() == before
        hardlink.unlink()
        manifest.chmod(0o666)
        assert run('activate').returncode != 0
        manifest.chmod(0o600)
        target = directory / 'original.json'
        manifest.rename(target)
        manifest.symlink_to(target)
        assert run('activate').returncode != 0
        assert target.read_bytes() == before
        manifest.unlink()
        original['instance_id'] = 'lab-b'
        manifest.write_text(json.dumps(original))
        assert run('activate').returncode != 0
    print('Runtime registration ownership tests passed')


if __name__ == '__main__':
    main()
