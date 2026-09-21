#!/usr/bin/env python3
"""Build shared example artifacts; never start graphs, guests or privileged labs."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import uuid

from example_cli import WorkflowError, private_dir, read_json, safe_path, source_files, write_json


def run(*args, capture=False):
    result = subprocess.run(list(map(str, args)), check=True, text=True,
                            stdout=subprocess.PIPE if capture else None)
    return result.stdout if capture else None


def fingerprint(source, platform):
    digest = hashlib.sha256(platform.encode())
    digest.update(run('git', '-C', source, 'rev-parse', 'HEAD', capture=True).encode())
    for relative in source_files(source):
        path = safe_path(source / relative)
        if path.is_file():
            digest.update(str(relative).encode() + b'\0' + path.read_bytes())
    return digest.hexdigest()


def verify(source, generation):
    release = source / 'scripts/release'
    run(sys.executable, release / 'image_release.py', 'verify', generation / 'images')
    run(sys.executable, release / 'guest_release.py', 'verify', '--guests', generation / 'guests', '--allow-dirty')
    sys.path.insert(0, str(release))
    from platform_bundle import verify as verify_platform
    manifests = list((generation / 'platform').glob('*.manifest.json'))
    if len(manifests) != 1:
        raise WorkflowError('expected one platform manifest')
    manifest = manifests[0]
    verify_platform(manifest.with_name(manifest.name.replace('.manifest.json', '.tar.gz')), manifest)
    inventory = read_json(generation / 'compiled.json')
    actual = {str(p.relative_to(generation / 'compiled')): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in (generation / 'compiled').rglob('*') if p.is_file()}
    if actual != inventory:
        raise WorkflowError('compiled example inventory changed')


def build(args):
    source, output = safe_path(args.source), safe_path(args.output)
    if output.is_relative_to(source):
        raise WorkflowError('example artifacts must be outside the checkout (QEMU build boundary)')
    output = private_dir(output)
    with os.fdopen(os.open(output / '.lock', os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600), 'w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        owner = output / 'owner.json'
        if owner.exists() and read_json(owner) != {'source': str(source)}:
            raise WorkflowError('artifact root belongs to another checkout')
        write_json(owner, {'source': str(source)})
        key = fingerprint(source, args.platform)
        current = output / 'current.json'
        if current.exists() and not args.fresh:
            record = read_json(current)
            generation = record.get('generation', '')
            if len(generation) != 32 or any(c not in '0123456789abcdef' for c in generation):
                raise WorkflowError('invalid artifact generation')
            if record.get('source_digest') == key:
                verify(source, safe_path(output / generation))
                print('Verified existing all-example build:', output / generation, flush=True)
                return
        # A failed build retains its own evidence and never replaces the last complete result.
        generation = private_dir(output / uuid.uuid4().hex)
        print('Building all example artifacts:', generation, flush=True)
        release = source / 'scripts/release'
        run(sys.executable, release / 'image_release.py', 'build', '--source', source,
            '--output', generation / 'images', '--platform', args.platform,
            '--with-vita', '--no-cache', '--allow-dirty')
        run(sys.executable, release / 'guest_release.py', 'build', '--source', source,
            '--catalog', generation / 'images/catalog', '--output', generation / 'guests', '--allow-dirty')
        run(sys.executable, release / 'platform_bundle.py', '--source', source,
            '--output', generation / 'platform', '--allow-dirty')
        for path in sorted((source / 'examples').rglob('graphx.yml')):
            name = path.parent.relative_to(source / 'examples')
            inputs = private_dir(generation / 'inputs' / name)
            authored = json.loads(run(args.graphx, 'config', 'authored', path,
                                      '--target', 'native-linux', capture=True))
            authored['catalog'] = os.path.relpath(generation / 'guests/catalog/lock.json', inputs)
            write_json(inputs / 'graphx.json', authored)
            private_dir((generation / 'compiled' / name).parent)
            private_dir((generation / 'credentials' / name).parent)
            run(args.graphx, 'compile', inputs / 'graphx.json', '--target', 'native-linux',
                '--source-root', inputs, '--catalog-root', generation / 'guests/catalog',
                '--credential-root', generation / 'credentials' / name,
                '--output', generation / 'compiled' / name)
        write_json(generation / 'compiled.json',
                   {str(p.relative_to(generation / 'compiled')): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in (generation / 'compiled').rglob('*') if p.is_file()})
        verify(source, generation)
        if fingerprint(source, args.platform) != key:
            raise WorkflowError('source changed during the build; result retained but not published')
        write_json(current, {'source_digest': key, 'generation': generation.name})
        print('Complete all-example build:', generation, flush=True)
        print('Images: images/; QEMU and catalog: guests/; native platform: platform/; graphs: compiled/', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'graphx', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--platform', choices=('linux/arm64', 'linux/amd64'), required=True)
    parser.add_argument('--fresh', action='store_true')
    try:
        build(parser.parse_args())
    except (WorkflowError, OSError, ValueError, subprocess.SubprocessError) as error:
        print('graphx example build:', error, file=sys.stderr)
        sys.exit(2)
