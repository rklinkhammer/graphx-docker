#!/usr/bin/env python3
"""Pinned catalog confinement and negative resource-boundary checks."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


def main(graphx, root):
    with tempfile.TemporaryDirectory(prefix='graphx-catalog-boundary-') as directory:
        folder = Path(directory).resolve()
        catalog = folder / 'catalog'
        shutil.copytree(root / 'config/catalog', catalog)
        graph = folder / 'graphx.yml'
        source = (root / 'examples/sample-pipeline/graphx.yml').read_text()
        graph.write_text(re.sub(r'^catalog:.*$', 'catalog: catalog/lock.json', source, flags=re.M))
        def check(code=None):
            result = subprocess.run([graphx, 'validate', graph, '--catalog-root', catalog], capture_output=True, text=True, timeout=15)
            assert (result.returncode == 0) if code is None else (result.returncode != 0 and code in result.stderr), result.stderr
        check()
        lockpath = catalog / 'lock.json'
        original_lock = lockpath.read_bytes()
        definition = catalog / 'types/sample.source.json'
        original_type = definition.read_bytes()
        definition.write_bytes(original_type + b' ')
        check('E_CATALOG_DIGEST')
        definition.write_bytes(original_type)
        definition.unlink()
        definition.symlink_to(root / 'config/catalog/types/sample.source.json')
        check('E_PATH_ESCAPE')
        definition.unlink()
        definition.write_bytes(original_type)
        lock = json.loads(original_lock)
        lock['files'].append(lock['files'][0])
        lockpath.write_text(json.dumps(lock))
        check('E_SCHEMA')
        lock = json.loads(original_lock)
        lock['files'][0]['path'] = '../escape.json'
        lockpath.write_text(json.dumps(lock))
        check('E_PATH_ESCAPE')
        lockpath.write_bytes(original_lock)
        # A re-pinned catalog must still satisfy type semantics.
        bad = json.loads(original_type)
        bad['parameters']['max_messages']['default'] = -1
        definition.write_text(json.dumps(bad))
        lock = json.loads(original_lock)
        for item in lock['files']:
            if item['path'] == 'types/sample.source.json':
                item['sha256'] = hashlib.sha256(definition.read_bytes()).hexdigest()
        lockpath.write_text(json.dumps(lock))
        check('E_SCHEMA')
        definition.write_bytes(original_type)
        lockpath.write_bytes(original_lock)
        guest = catalog / 'guests/echo-x86.json'
        bad = json.loads(guest.read_text()); bad['outputs'] = 'not-artifacts'
        guest.write_text(json.dumps(bad))
        lock = json.loads(original_lock)
        for item in lock['files']:
            if item['path'] == 'guests/echo-x86.json': item['sha256'] = hashlib.sha256(guest.read_bytes()).hexdigest()
        lockpath.write_text(json.dumps(lock))
        check('E_SCHEMA')
    print('catalog digest, path escape, symlink, duplicate pin, malformed default and guest shape checks passed')


if __name__ == '__main__':
    main(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
