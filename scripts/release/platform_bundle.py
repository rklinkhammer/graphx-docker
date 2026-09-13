#!/usr/bin/env python3
"""Build a reproducible companion bundle: pinned Node, telemetry modules and web assets."""
from __future__ import annotations
import argparse
import datetime as dt
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request

from release_common import (ReleaseError, bounded_bytes, current_platform, inspect_archive,
                            sha256_file, source_version, validate_epoch, spdx_id)


def encoded(value):
    return (json.dumps(value, sort_keys=True, indent=2) + '\n').encode()


def verify(archive: Path, manifest_path: Path, expected: dict | None = None):
    manifest = json.loads(bounded_bytes(manifest_path, 4 * 1024 * 1024, 'platform bundle manifest'))
    if expected and any(manifest.get(key) != value for key, value in expected.items()):
        raise ReleaseError('platform bundle release identity mismatch')
    if manifest['sha256'] != sha256_file(archive):
        raise ReleaseError('platform bundle checksum mismatch')
    files = manifest['files']
    prefix = f"graphx-{manifest['graphx_version']}-{manifest['platform']}"
    for required in ['bin/graphx-platform', 'libexec/graphx-platform/node',
                     'libexec/graphx-platform/apps/telemetry/platform.mjs',
                     'libexec/graphx-platform/web/dist/index.html', 'libexec/graphx-platform/bundle.spdx.json']:
        if prefix + '/' + required not in files:
            raise ReleaseError('platform bundle is missing a required component')
    inspection = inspect_archive(archive, captures={name: 256 * 1024 * 1024 for name in files})
    if set(inspection.regular_files) != set(files):
        raise ReleaseError('platform bundle inventory mismatch')
    for name, entry in files.items():
        if hashlib.sha256(inspection.captured[name]).hexdigest() != entry['sha256'] or inspection.regular_file_modes[name] != entry['mode']:
            raise ReleaseError('platform bundle member mismatch')
    return manifest


def build(source: Path, output: Path, node_archive: Path | None = None, epoch: int = 0, allow_dirty: bool = False):
    version, platform = source_version(source), current_platform()
    dirty = bool(subprocess.check_output(['git','status','--porcelain'],cwd=source,text=True).strip())
    if dirty and not allow_dirty:
        raise ReleaseError('platform release builds require a clean worktree; use --allow-dirty for a local candidate')
    pin = json.loads((source / 'scripts/release/node-runtime.json').read_text())
    selected = pin['archives'][platform]
    output.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(prefix='graphx-platform-release-') as temp:
        temporary = Path(temp)
        if node_archive is None:
            node_archive = temporary / selected['name']
            with urllib.request.urlopen(f"https://nodejs.org/dist/v{pin['version']}/{selected['name']}", timeout=30) as response:
                with node_archive.open('wb') as target:
                    remaining = 128 * 1024 * 1024
                    while chunk := response.read(min(1024 * 1024, remaining + 1)):
                        remaining -= len(chunk)
                        if remaining < 0:
                            raise ReleaseError('Node archive exceeds download bound')
                        target.write(chunk)
        if sha256_file(node_archive) != selected['sha256']:
            raise ReleaseError('Node runtime differs from reviewed release pin')
        root_name = f'graphx-{version}-{platform}'
        root = temporary / root_name
        bundle = root / 'libexec/graphx-platform'
        bundle.mkdir(parents=True)
        with tarfile.open(node_archive, 'r:gz') as archive:
            prefix = selected['name'].removesuffix('.tar.gz')
            for relative, destination in [('bin/node', bundle / 'node'), ('LICENSE', bundle / 'NODE-LICENSE')]:
                member = archive.getmember(prefix + '/' + relative)
                if not member.isfile() or member.size > 256 * 1024 * 1024:
                    raise ReleaseError('invalid pinned Node archive member')
                destination.write_bytes(archive.extractfile(member).read())
        (bundle / 'node').chmod(0o755)
        if subprocess.check_output([str(bundle / 'node'), '--version'], text=True).strip() != 'v' + pin['version']:
            raise ReleaseError('Node executable version differs from pin')
        app = bundle / 'apps/telemetry'; app.mkdir(parents=True)
        for path in (source / 'apps/telemetry').glob('*.mjs'):
            if not path.name.endswith('.test.mjs') and path.name not in {'dev.mjs', 'test-config.mjs'}:
                shutil.copyfile(path, app / path.name)
        for name in ('package.json', 'package-lock.json'):
            shutil.copyfile(source / 'apps/telemetry' / name, app / name)
        web = bundle / 'web'
        shutil.copytree(source / 'web', web, ignore=shutil.ignore_patterns('node_modules', 'dist'))
        environment = {**os.environ, 'PATH': str(bundle) + os.pathsep + os.environ['PATH'],
                       'SOURCE_DATE_EPOCH': str(epoch), 'TZ': 'UTC'}
        subprocess.run(['npm', 'ci', '--omit=dev', '--ignore-scripts'], cwd=app, env=environment, check=True)
        subprocess.run(['npm', 'ci'], cwd=web, env=environment, check=True)
        subprocess.run(['npm', 'run', 'build'], cwd=web, env=environment, check=True)
        for path in list(web.iterdir()):
            if path.name not in {'dist', 'package-lock.json'}:
                if path.is_dir(): shutil.rmtree(path)
                else: path.unlink()
        # npm creates only development command links in this dependency set; runtime needs regular modules.
        shutil.rmtree(app / 'node_modules/.bin', ignore_errors=True)
        schema = bundle / 'config/schema'; schema.mkdir(parents=True)
        shutil.copyfile(source / 'config/schema/normalized-graph.schema.json', schema / 'normalized-graph.schema.json')
        for name in ('LICENSE', 'THIRD_PARTY.md'):
            shutil.copyfile(source / name, bundle / name)
        (root / 'bin').mkdir()
        launcher = root / 'bin/graphx-platform'
        launcher.write_text('#!/bin/sh\nplatform_bin=$(CDPATH="" cd -- "$(dirname -- "$0")" && pwd)\n'
                            'PATH="$platform_bin:$PATH"\nexport PATH\n'
                            'exec "$platform_bin/../libexec/graphx-platform/node" '
                            '"$platform_bin/../libexec/graphx-platform/apps/telemetry/platform.mjs" "$@"\n')
        launcher.chmod(0o755)
        packages = [{'name': 'node', 'version': pin['version'], 'sha256': selected['sha256'], 'license': 'MIT'}]
        for lock in (app / 'package-lock.json', web / 'package-lock.json'):
            for name, package in json.loads(lock.read_text())['packages'].items():
                if name and not package.get('dev'):
                    packages.append({'name': name, 'version': package['version'], 'license': package.get('license', 'NOASSERTION')})
        commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=source, text=True).strip()
        spdx_packages = {spdx_id(package['name'], package['version']): package for package in packages}
        (bundle / 'bundle.spdx.json').write_bytes(encoded({
            'spdxVersion':'SPDX-2.3', 'dataLicense':'CC0-1.0', 'SPDXID':'SPDXRef-DOCUMENT',
            'name':f'graphx-platform-{version}-{platform}',
            'documentNamespace':f'https://graphx.invalid/releases/{commit}/{platform}/platform',
            'creationInfo': {'created':dt.datetime.fromtimestamp(epoch, dt.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
                             'creators':['Tool: graphx-platform-bundle/1']},
            'packages':[{'SPDXID':identifier, 'name':package['name'], 'versionInfo':package['version'],
                         'downloadLocation':'NOASSERTION', 'filesAnalyzed':False,
                         'licenseDeclared':package['license'], 'licenseConcluded':'NOASSERTION'}
                        for identifier, package in sorted(spdx_packages.items())],
            'relationships':[{'spdxElementId':'SPDXRef-DOCUMENT', 'relationshipType':'DESCRIBES',
                              'relatedSpdxElement':identifier} for identifier in sorted(spdx_packages)]}))
        (bundle / 'dependencies.json').write_bytes(encoded({'node_archive': selected, 'packages': packages}))
        archive = output / f'graphx-platform-{version}-{platform}.tar.gz'
        files = {}
        with archive.open('xb') as stream, gzip.GzipFile(filename='', fileobj=stream, mode='wb', mtime=epoch) as compressed, tarfile.open(fileobj=compressed, mode='w') as target:
            for path in sorted(root.rglob('*')):
                if path.is_symlink(): raise ReleaseError('unexpected symlink in platform bundle')
                if not path.is_file(): continue
                relative = str(path.relative_to(temporary)); payload = path.read_bytes()
                mode = 0o755 if path in (launcher, bundle / 'node') else 0o644
                info = tarfile.TarInfo(relative); info.size = len(payload); info.mode = mode; info.mtime = epoch
                target.addfile(info, io.BytesIO(payload))
                files[relative] = {'sha256': hashlib.sha256(payload).hexdigest(), 'mode': mode}
        manifest = output / f'graphx-platform-{version}-{platform}.manifest.json'
        manifest.write_bytes(encoded({'version': 1, 'graphx_version': version, 'platform': platform, 'commit':commit, 'epoch':epoch, 'dirty_candidate':dirty,
                                     'node': selected, 'sha256': sha256_file(archive), 'files': files}))
        verify(archive, manifest)
        return archive


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path)
    parser.add_argument('--verify', type=Path)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--expected-commit')
    parser.add_argument('--expected-platform')
    parser.add_argument('--expected-version')
    parser.add_argument('--expected-epoch', type=int)
    parser.add_argument('--node-archive', type=Path)
    parser.add_argument('--allow-dirty', action='store_true')
    parser.add_argument('--epoch', type=int, default=0)
    args = parser.parse_args()
    if args.verify:
        expected = {'commit':args.expected_commit, 'platform':args.expected_platform,
                    'graphx_version':args.expected_version, 'epoch':args.expected_epoch}
        if not args.manifest or any(value is None for value in expected.values()):
            parser.error('verification requires a manifest and all expected release identity values')
        if not args.allow_dirty: expected['dirty_candidate'] = False
        verify(args.verify, args.manifest, expected)
        print('Platform bundle release identity, inventory, modes and checksums verified')
    else:
        if not args.output: parser.error('--output is required for build')
        print(build(args.source.resolve(), args.output.resolve(), args.node_archive, validate_epoch(args.epoch), args.allow_dirty))
