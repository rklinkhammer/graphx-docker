#!/usr/bin/env python3
"""Common workspace workflow. Authored graphs and graphx run remain authoritative."""
from __future__ import annotations

import argparse
import contextlib
import fcntl
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import secrets
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import uuid
import urllib.request
import urllib.error
import urllib.parse
import webbrowser


class WorkflowError(RuntimeError):
    pass


def run(argv, *, capture=False, input=None, cwd=None):
    result = subprocess.run(list(map(str, argv)), cwd=cwd, input=input, text=True,
                            stdout=subprocess.PIPE if capture else sys.stderr,
                            stderr=sys.stderr)
    if result.returncode:
        raise WorkflowError(f'command failed (exit {result.returncode}): {argv[0]}')
    return result.stdout.strip() if capture else ''


def safe_path(path):
    path = Path(os.path.abspath(path))
    if any(p.is_symlink() for p in [path, *path.parents]):
        raise WorkflowError(f'symlink path refused: {path}')
    return path


def private_dir(path):
    path = safe_path(path)
    path.mkdir(parents=True, exist_ok=True, mode=0o700)
    info = path.stat()
    if info.st_uid != os.getuid() or info.st_mode & 0o022:
        raise WorkflowError(f'workspace must be owned by the current user and not writable by others: {path}')
    return path


@contextlib.contextmanager
def lima_lock():
    # Shared across checkouts and --workspace selections: the VM is host-wide.
    folder = private_dir(Path.home() / '.graphx')
    path = safe_path(folder / 'lima.lock')
    with os.fdopen(os.open(path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW | os.O_NONBLOCK, 0o600), 'w') as lock:
        info = os.fstat(lock.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_nlink != 1 or info.st_mode & 0o077:
            raise WorkflowError('invalid Lima lifecycle lock')
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise WorkflowError('another GraphX command is using Lima; retry when it completes') from error
        yield


def read_json(path):
    path = safe_path(path)
    with os.fdopen(os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)) as stream:
        info = os.fstat(stream.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_size > 16 * 1024 * 1024:
            raise WorkflowError('invalid JSON file boundary')
        return json.load(stream)


def write_json(path, value):
    path = safe_path(path)
    temporary = path.with_name('.' + path.name + '-' + uuid.uuid4().hex)
    with os.fdopen(os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'w') as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write('\n')
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)
    directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


def name_path(name):
    if not re.fullmatch(r'[a-z0-9][a-z0-9_-]*(/[a-z0-9][a-z0-9_-]*)*', name):
        raise WorkflowError('invalid example name')
    return name


def source_files(source):
    output = subprocess.check_output(['git', '-C', str(source), 'ls-files', '-co', '--exclude-standard', '-z'])
    return sorted(set(Path(os.fsdecode(p)) for p in output.split(b'\0') if p))


def fingerprint(source):
    digest = hashlib.sha256()
    for relative in source_files(source):
        path = source / relative
        if path.is_symlink():
            raise WorkflowError(f'source symlinks are unsupported: {relative}')
        if path.is_file() and path.suffix != '.md' and relative.parts[0] != 'tests':
            digest.update(str(relative).encode() + b'\0' + path.read_bytes())
    return digest.hexdigest()[:20]


def grants(authored, specifications):
    value = json.loads(json.dumps(authored))
    if not specifications:
        return value
    selected = []
    for specification in specifications:
        node, separator, actions = specification.partition(':')
        action_list = actions.split(',')
        allowed = ('reset',) if node == 'collector' else ('pause', 'resume', 'serial')
        if not separator or (node != 'collector' and node not in value['nodes']) or any(
                action not in allowed for action in action_list):
            raise WorkflowError('--control requires NODE:pause,resume,serial for a declared node or collector:reset (serial requires QEMU)')
        selected.append({'credential': 'example-operator', 'nodes': [node], 'actions': action_list})
    credentials = value.setdefault('credentials', {})
    if 'example-operator' in credentials:
        raise WorkflowError('example-operator credential name is reserved by this explicit selection')
    credentials['example-operator'] = {'identity': 'operator', 'provider': 'external', 'members': ['token']}
    value.setdefault('platform', {})['control'] = {'enabled': True, 'grants': selected}
    return value


def open_console(result, operator=None):
    """Exchange owned credentials for a short-lived browser handoff; never log it."""
    parsed = urllib.parse.urlsplit(result['console'])
    if parsed.scheme not in ('http', 'https') or parsed.hostname not in ('127.0.0.1', '::1') or parsed.username or parsed.password:
        raise WorkflowError('automatic console login requires a loopback console URL')
    selected = result.get('control_token')
    if isinstance(selected, dict):
        if operator:
            if operator not in selected:
                raise WorkflowError('--operator must name an existing control credential')
            selected = selected[operator]
        else:
            selected = None
            print('Opening observation access; select --operator REF for one of the existing control grants.', file=sys.stderr)
    elif operator:
        scope = result.get('control_scope', [])
        if not isinstance(scope, list) or not any(grant['credential'] == operator for grant in scope):
            raise WorkflowError('--operator must name an existing control credential')
    request = urllib.request.Request(result['console'] + '/api/console/handoff',
        data=json.dumps({'control_token': selected}).encode(), headers={
            'Authorization': 'Bearer ' + result['observation_token'],
            'Content-Type': 'application/json', 'Origin': result['console']})
    # Do not follow redirects while holding bearer credentials.
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, *args, **kwargs):
            return None
    try:
        with urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect()).open(request, timeout=10) as response:
            value = json.loads(response.read(4096))
        code = value['code']
        if not isinstance(code, str) or not re.fullmatch(r'[A-Za-z0-9_-]{43}', code):
            raise ValueError('invalid handoff')
    except (OSError, ValueError, KeyError) as error:
        raise WorkflowError('console login unavailable; use images built from this checkout and retry example open') from error
    if not webbrowser.open(result['console'] + '/#graphx-login=' + code):
        raise WorkflowError('no browser opened; run example open in a desktop session or use example tokens')


def present_result(result, args):
    automatic = args.action in ('up', 'open') and not getattr(args, 'no_open', False) and not args.json
    if automatic:
        try:
            open_console(result, getattr(args, 'operator', None))
            result = {**result, 'browser': 'opened'}
        except WorkflowError as error:
            if args.action == 'open':
                raise
            print(f'Example is running. {error}', file=sys.stderr)
            result = {**result, 'browser': 'unavailable'}
        result = {key: value for key, value in result.items() if key not in ('observation_token', 'control_token')}
    emit(result, args.json)


def emit(value, as_json):
    if as_json:
        print(json.dumps(value, sort_keys=True))
        return
    for key, item in value.items():
        label = key.replace('_', ' ').capitalize()
        print(f'{label}: {json.dumps(item) if isinstance(item, (dict, list)) else item}')


class Workflow:
    def __init__(self, source, cli, args):
        self.source, self.cli, self.args = source, cli, args
        self.base = safe_path(args.workspace or (source / 'outputs/examples' if sys.platform == 'darwin' else '/var/lib/graphx/examples'))
        identifier = name_path(args.instance or args.name)
        if args.action in ('status', 'tokens', 'logs', 'down', 'scenario'):
            targets = [args.target] if args.target else ['orbstack', 'native-macos', 'native-linux', 'lima']
            found = [target for target in targets if any((self.base / target / identifier / filename).exists()
                     for filename in ('current.json', 'remote.json'))]
            if len(found) > 1:
                raise WorkflowError('multiple prepared targets; select --target explicitly')
            if found:
                self.target = found[0]
                folder = safe_path(self.base / self.target / identifier)
                if sys.platform == 'darwin' and self.target == 'lima':
                    self.privileged, self.containers, self.native, self.guests = True, False, False, False
                else:
                    record = read_json(folder / 'current.json')
                    if not re.fullmatch(r'[a-f0-9]{32}', record.get('generation', '')):
                        raise WorkflowError('invalid launch generation')
                    resolved = read_json(folder / record['generation'] / 'compiled/resolved.json')
                    kinds = {node['execution']['kind'] for node in resolved['nodes']}
                    self.privileged = bool(resolved['network']['switches']) or bool(kinds & {'namespace', 'qemu'})
                    self.containers = 'container' in kinds
                    self.native = bool(kinds & {'native', 'namespace', 'qemu'})
                    self.guests = 'qemu' in kinds
                self.key = None
                return
        self.original = safe_path(source / 'examples' / name_path(args.name) / 'graphx.yml')
        if not self.original.is_file():
            raise WorkflowError(f'unknown example: {args.name}; use graphx example list')
        self.authored = json.loads(run([cli, 'config', 'authored', self.original,
                                      '--target', 'native-linux'], capture=True))
        self.normal = json.loads(run([cli, 'config', 'normalize', self.original,
                                     '--target', 'native-linux'], capture=True))
        kinds = {node['execution']['kind'] for node in self.normal['nodes']}
        self.privileged = bool(self.normal['network']['switches']) or bool(kinds & {'namespace', 'qemu'})
        self.containers = 'container' in kinds
        self.native = bool(kinds & {'native', 'namespace', 'qemu'})
        self.guests = 'qemu' in kinds
        self.target = args.target or ('lima' if self.privileged else 'orbstack' if self.containers else 'native-macos') if sys.platform == 'darwin' else args.target or 'native-linux'
        self.base = safe_path(args.workspace or (source / 'outputs/examples' if sys.platform == 'darwin' else '/var/lib/graphx/examples'))
        self.key = fingerprint(source) if args.action in ('prepare', 'up') else None

    def lima(self):
        if self.privileged and not self.args.allow_privileged:
            raise WorkflowError('this operation requires --allow-privileged on Linux/Lima')
        with lima_lock():
            if self.args.action in ('prepare', 'up'):
                run([self.source / 'infrastructure/lima/start.sh'])
            else:
                state = run(['bash', '-c',
                             'source "$1"; graphx_lima_require_host; graphx_lima_assert_identity "$(graphx_lima_digest)"',
                             'graphx-env', self.source / 'infrastructure/lima/common.sh'], capture=True)
                if state != 'Running':
                    raise WorkflowError('GraphX Lima is stopped; use example up to start a demo, or graphx env up to inspect retained state')
            result = self.lima_dispatch()
            if result == 0 and self.args.action in ('prepare', 'down'):
                self.lima_stop_if_idle()
            return result

    def lima_stop_if_idle(self):
        probe = self.source / 'infrastructure/lima/idle.py'
        try:
            result = subprocess.run(
                ['limactl', 'shell', '--workdir', '/var/lib/graphx', 'graphx', '--',
                 'sudo', '-n', 'python3', '-'], input=probe.read_text(), text=True,
                stdout=sys.stderr, stderr=sys.stderr, timeout=45)
            if result.returncode != 0:
                print('Lima remains running: workloads remain or idle state could not be verified.', file=sys.stderr)
                return
        except (OSError, subprocess.SubprocessError) as error:
            print(f'Lima remains running: idle check failed: {error}', file=sys.stderr)
            return
        # stop.sh rechecks the complete VM identity immediately before stopping.
        run([self.source / 'infrastructure/lima/stop.sh'])

    def lima_dispatch(self):
        prefix = ['limactl', 'shell', '--workdir', '/var/lib/graphx', 'graphx', '--']
        remote_base = '/var/lib/graphx/examples'
        # A source snapshot includes Git metadata for the existing release builders.
        route_folder = private_dir(self.base / 'lima' / name_path(self.args.instance or self.args.name))
        route_file = route_folder / 'remote.json'
        remote_source = f'{remote_base}/sources/{fingerprint(self.source)}'
        if self.args.action in ('status', 'tokens', 'logs', 'down', 'scenario') and route_file.exists():
            remote_source = read_json(route_file)['source']
            if not re.fullmatch(re.escape(remote_base) + r'/sources/[a-f0-9]{20}', remote_source):
                raise WorkflowError('invalid remote source reference')
        exists = subprocess.run([*prefix, 'test', '-f', remote_source + '/.graphx-source-ready']).returncode == 0
        if not exists:
            if self.args.action not in ('prepare', 'up', 'plan', 'open'):
                raise WorkflowError('source snapshot is not prepared; run example prepare first')
            run([*prefix, 'mkdir', '-p', remote_source])
            with tempfile.TemporaryFile() as archive:
                with tarfile.open(fileobj=archive, mode='w') as tar:
                    for relative in source_files(self.source):
                        path = safe_path(self.source / relative)
                        if path.is_file():
                            tar.add(path, arcname=str(relative), recursive=False)
                    if not (self.source / '.git').is_dir():
                        raise WorkflowError('Lima source preparation requires a full checkout, not a Git worktree')
                    tar.add(self.source / '.git', arcname='.git')
                archive.seek(0)
                subprocess.run([*prefix, 'tar', '-C', remote_source, '-xf', '-'], stdin=archive,
                               stdout=sys.stderr, stderr=sys.stderr, check=True)
            run([*prefix, 'touch', remote_source + '/.graphx-source-ready'])
        remote_cli = remote_source + '/build/example/graphx'
        if subprocess.run([*prefix, 'test', '-x', remote_cli]).returncode:
            run([*prefix, 'cmake', '-S', remote_source, '-B', remote_source + '/build/example',
                  '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', '-DGRAPHX_BUILD_TESTS=OFF'])
            run([*prefix, 'cmake', '--build', remote_source + '/build/example', '--target', 'graphx-cli', '-j', '4'])
        forwarded = [remote_cli, 'example', self.args.action, self.args.name, '--target', 'lima', '--workspace', remote_base]
        for flag in ('allow_privileged', 'restart', 'json', 'follow', 'fresh_images'):
            if getattr(self.args, flag, False):
                forwarded.append('--' + flag.replace('_', '-'))
        for flag in ('instance', 'node', 'external', 'images', 'release', 'catalog', 'laboratory', 'operation'):
            value = getattr(self.args, flag)
            if value:
                forwarded += ['--' + flag, str(value)]
        if self.args.scenario_action:
            forwarded += ['--action', self.args.scenario_action]
        for grant in self.args.control:
            forwarded += ['--control', grant]
        # Source is selected explicitly; no Docker socket is forwarded.
        forwarded += ['--source', remote_source]
        write_json(route_file, {'source': remote_source})
        if self.args.action in ('up', 'open'):
            if '--json' not in forwarded:
                forwarded.append('--json')
            forwarded.append('--no-open')
            result = subprocess.run([*prefix, *forwarded], stdout=subprocess.PIPE, text=True)
            if result.returncode == 0:
                present_result(json.loads(result.stdout), self.args)
            return result.returncode
        result = subprocess.run([*prefix, *forwarded])
        return result.returncode

    def command(self, action, record, capture=False):
        generation = self.generation(record)
        verb = ['scenario', self.args.operation, '--action', self.args.scenario_action] if action == 'scenario' else ['run', action]
        command = [self.cli, *verb, '--output', generation / 'compiled', '--state-root', generation / 'state']
        if record['privileged']:
            if not self.args.allow_privileged:
                raise WorkflowError('this operation requires --allow-privileged on Linux/Lima')
            command += ['--allow-privileged']
            if os.geteuid() != 0:
                command = ['sudo', *command]
        for name in ('images', 'release'):
            if record.get(name):
                command += ['--' + name, record[name]]
        if record.get('release'):
            command += ['--credentials', generation / 'credentials']
        if record.get('external'):
            command += ['--external', record['external']]
        return run(command, capture=capture)

    def generation(self, record):
        if set(record) != {'generation', 'privileged', 'images', 'release', 'external', 'selection'} or not re.fullmatch(r'[a-f0-9]{32}', record['generation']):
            raise WorkflowError('invalid launch reference')
        if type(record['privileged']) is not bool or record['privileged'] != self.privileged:
            raise WorkflowError('launch privilege identity mismatch')
        return safe_path(self.folder / record['generation'])

    def artifacts(self):
        fresh = getattr(self.args, 'fresh_images', False)
        cache_key = self.key + ('-' + uuid.uuid4().hex if fresh else '')
        cache = private_dir(self.base / 'artifacts' / cache_key)
        vita = any(n['type'].startswith('vita.') for n in self.normal['nodes'])
        images = safe_path(self.args.images) if self.args.images else cache / ('images-vita' if vita else 'images')
        release = safe_path(self.args.release) if self.args.release else cache / 'release'
        if self.args.images and not (images / 'images.json').is_file():
            raise WorkflowError('--images must name an existing verified image release')
        if self.args.release and not (release / 'release.json').is_file():
            raise WorkflowError('--release must name an existing verified installation')
        if self.containers or self.guests:
            if not images.exists():
                machine = 'arm64' if platform.machine() in ('arm64', 'aarch64') else 'amd64'
                optional = ['--with-vita'] if vita else []
                run([sys.executable, self.source / 'scripts/release/image_release.py', 'build',
                     '--output', images, '--platform', 'linux/' + machine, '--allow-dirty', '--no-cache', *optional])
            run([sys.executable, self.source / 'scripts/release/image_release.py', 'verify', images])
            if vita:
                manifest = read_json(images / 'images.json')
                if 'vita' not in manifest['images'] or manifest.get('qualification_hooks') is not False:
                    raise WorkflowError('VITA examples require --with-vita images without qualification hooks')
        if self.native and not release.exists():
            native, companion = cache / 'native', cache / 'companion'
            run([sys.executable, self.source / 'scripts/release/build_release.py', '--source', self.source,
                 '--build-dir', cache / 'native-build', '--output-dir', native, '--allow-dirty'])
            epoch = run(['git', '-C', self.source, 'show', '-s', '--format=%ct', 'HEAD'], capture=True)
            commit = run(['git', '-C', self.source, 'rev-parse', 'HEAD'], capture=True)
            version = (self.source / 'VERSION').read_text().strip()
            run([sys.executable, self.source / 'scripts/release/platform_bundle.py', '--source', self.source,
                 '--output', companion, '--epoch', epoch, '--allow-dirty'])
            run([sys.executable, self.source / 'scripts/release/install_release.py', '--native', native,
                 '--companion', companion, '--output', release, '--commit', commit, '--version', version,
                 '--epoch', epoch, '--allow-dirty'])
        if self.guests and not self.args.release:
            guests = cache / 'guests'
            combined = cache / 'guest-release'
            if not combined.exists():
                run([sys.executable, self.source / 'scripts/release/guest_release.py', 'build',
                     '--source', self.source, '--catalog', images / 'catalog', '--output', guests, '--allow-dirty'])
                run([sys.executable, self.source / 'scripts/release/guest_release.py', 'install',
                     '--native', release, '--guests', guests, '--output', combined, '--allow-dirty'])
            release = combined
            images_catalog = guests / 'catalog'
        else:
            images_catalog = images / 'catalog' if self.containers else self.source / 'config/catalog'
        if self.args.catalog:
            images_catalog = safe_path(self.args.catalog)
        return images if self.containers or self.guests else None, release if self.native else None, images_catalog

    def prepare(self, authored, selection):
        cache = private_dir(self.base / 'artifacts' / self.key)
        with os.fdopen(os.open(safe_path(cache / '.lock'), os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600), 'w') as lock:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as error:
                raise WorkflowError('another preparation owns the shared artifact cache') from error
            images, release, catalog = self.artifacts()
        generation = private_dir(self.folder / uuid.uuid4().hex)
        identifier = self.args.instance.replace('/', '-') if self.args.instance else authored['graph']['id']
        authored['graph']['id'] = identifier[:50] + '-' + generation.name[:10]
        authored['catalog'] = os.path.relpath(catalog / 'lock.json', generation / 'input')
        private_dir(generation / 'input')
        write_json(generation / 'input/graphx.yml', authored)
        command = [self.cli, 'compile', generation / 'input/graphx.yml', '--target', self.target,
                   '--catalog-root', catalog, '--source-root', generation / 'input',
                   '--credential-root', generation / 'credentials', '--output', generation / 'compiled']
        if self.args.laboratory:
            command += ['--laboratory', self.args.laboratory]
        run(command)
        external = str(safe_path(self.args.external)) if self.args.external else ''
        if self.args.control:
            directory = private_dir(generation / 'external')
            if external:
                # Copy only declared files, with no links or special files.
                for entry in Path(external).rglob('*'):
                    safe_path(entry)
                    if entry.is_file():
                        destination = directory / entry.relative_to(external)
                        private_dir(destination.parent)
                        with os.fdopen(os.open(destination, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'wb') as stream:
                            stream.write(entry.read_bytes())
                    elif not entry.is_dir():
                        raise WorkflowError('special external credential file refused')
            operator = private_dir(directory / 'example-operator') / 'token'
            with os.fdopen(os.open(operator, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'w') as stream:
                stream.write(secrets.token_urlsafe(48))
            external = str(directory)
        record = {'generation': generation.name, 'privileged': self.privileged,
                  'images': str(images or ''), 'release': str(release or ''),
                  'external': external, 'selection': selection}
        write_json(self.folder / 'current.json', record)
        return record

    def credentials(self, record, resolved):
        self.command('status', record)  # verifies compiled artifacts and owned resources first
        graph = resolved['graph_id']
        if self.containers or self.guests:
            def read(reference):
                return run(['docker', 'exec', 'graphx-' + graph + '-platform', 'cat',
                            '/run/secrets/' + reference + '/token'], capture=True)
        else:
            def read(reference):
                path = safe_path(self.generation(record) / 'credentials' / reference / 'token')
                with os.fdopen(os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)) as stream:
                    info = os.fstat(stream.fileno())
                    if not stat.S_ISREG(info.st_mode) or info.st_size > 8192:
                        raise WorkflowError('invalid token boundary')
                    return stream.read().strip()
        manifest = read_json(self.generation(record) / 'compiled/credentials.json')
        observers = [ref for ref, entry in manifest['entries'].items() if entry['identity'] == 'reader' and 'token' in entry['members']]
        controls = resolved['platform']['control']
        refs = sorted({g['credential'] for g in controls.get('grants', [])}) if controls['enabled'] else []
        tokens = {ref: read(ref) for ref in refs}
        return {'observation_token': read(observers[0]) if observers else None,
                'control_token': next(iter(tokens.values())) if len(tokens) == 1 else tokens or None,
                'control_scope': controls.get('grants', []) if controls['enabled'] else 'disabled'}

    def local(self):
        if self.privileged and self.args.action in ('prepare', 'up') and not self.args.allow_privileged:
            raise WorkflowError('OVS/guest preparation requires explicit --allow-privileged')
        if self.target == 'orbstack':
            if run(['docker', 'context', 'show'], capture=True) != 'orbstack':
                raise WorkflowError('select the OrbStack Docker context first')
        identifier = name_path(self.args.instance or self.args.name)
        self.folder = private_dir(self.base / self.target / identifier)
        lock_path = safe_path(self.folder / '.lock')
        with os.fdopen(os.open(lock_path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600), 'w') as lock:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError as error:
                raise WorkflowError('another example operation owns this launch lock') from error
            current = self.folder / 'current.json'
            record = read_json(current) if current.exists() else None
            if record:
                self.generation(record)
            if self.args.action == 'plan':
                emit({'example': self.args.name, 'target': self.target, 'privileged': self.privileged,
                      'preparation': ['verified images'] * self.containers + ['verified native release'] * self.native + ['verified guest artifacts'] * self.guests,
                      'prepared': bool(record), 'control': self.args.control}, self.args.json)
                if record:
                    run([self.cli, 'run', 'plan', '--output', self.generation(record) / 'compiled',
                         '--state-root', self.generation(record) / 'state'])
                return 0
            if self.args.action in ('prepare', 'up'):
                fresh = getattr(self.args, 'fresh_images', False)
                if fresh and record and not self.args.restart:
                    raise WorkflowError('fresh images for an existing instance require --restart')
                if record:
                    for field in ('images', 'release', 'external', 'catalog'):
                        previous = record['selection'].get('inputs', {}).get(field)
                        if getattr(self.args, field) is None and previous and not (fresh and field in ('images', 'catalog', 'release')):
                            setattr(self.args, field, Path(previous))
                    if self.args.laboratory is None:
                        self.args.laboratory = record['selection'].get('laboratory')
                authored = grants(self.authored, self.args.control)
                selection = {'source': self.key, 'control': self.args.control, 'laboratory': self.args.laboratory,
                             'inputs': {field: str(getattr(self.args, field) or '') for field in ('images', 'release', 'external', 'catalog')}}
                # Omitted control options retain the already selected grant.
                compare = {**selection, 'control': record['selection']['control']} if record and not self.args.control else selection
                if record and (compare != record['selection'] or fresh):
                    if not self.args.restart:
                        raise WorkflowError('source or selection changed; use --restart or a new --instance')
                    self.command('down', record)
                    if not self.args.control and record['selection']['control']:
                        authored = grants(self.authored, record['selection']['control'])
                        self.args.control = record['selection']['control']
                        selection['control'] = self.args.control
                    record = None
                elif record and self.args.restart:
                    self.command('down', record)
                if not record:
                    record = self.prepare(authored, selection)
                if self.args.action == 'prepare':
                    emit({'status': 'prepared', 'output': str(self.generation(record) / 'compiled')}, self.args.json)
                    return 0
                status = self.command('status', record, capture=True)
                active = any(line.startswith('container ') or line.endswith(' running') for line in status.splitlines())
                if not active:
                    self.command('up', record)
            elif not record:
                raise WorkflowError('example has not been prepared; run example up first')
            if self.args.action == 'scenario':
                if not self.args.scenario_action:
                    raise WorkflowError('scenario requires --action ID')
                self.command('scenario', record)
                emit({'status': 'completed', 'action': self.args.scenario_action, 'operation': self.args.operation}, self.args.json)
                return 0
            if self.args.action == 'down':
                self.command('down', record)
                emit({'status': 'stopped', 'example': self.args.name}, self.args.json)
                return 0
            resolved = read_json(self.generation(record) / 'compiled/resolved.json')
            if self.args.action == 'logs':
                self.command('status', record)
                nodes = {n['node_id'] for n in resolved['nodes']}
                node = self.args.node or ('platform' if self.guests else 'sink' if 'sink' in nodes else sorted(nodes)[0])
                if node not in nodes and node != 'platform':
                    raise WorkflowError('unknown node')
                selected = next((n for n in resolved['nodes'] if n['node_id'] == node), None)
                kind = selected['execution']['kind'] if selected else 'container' if self.containers or self.guests else 'native'
                if kind == 'qemu':
                    raise WorkflowError('guest telemetry is available with --node platform; raw boot logs remain in the owned guest runtime directory')
                if kind == 'container':
                    command = ['docker', 'logs', '--tail', '50', *(['--follow'] if self.args.follow else []),
                               'graphx-' + resolved['graph_id'] + '-' + node]
                else:
                    command = ['tail', '-n', '50', *(['-f'] if self.args.follow else []),
                               self.generation(record) / 'state' / resolved['graph_id'] / 'logs' / (node + '.log')]
                if kind != 'container' and record['privileged'] and os.geteuid() != 0:
                    command = ['sudo', *command]
                return subprocess.call(list(map(str, command)))
            port = resolved['platform']['console']['port']
            result = {'graph': resolved['graph_id'], 'target': self.target,
                      'console': f'http://127.0.0.1:{18080 if self.target == "lima" and port == 8080 else port}'}
            if self.args.action in ('up', 'tokens', 'open'):
                result.update(self.credentials(record, resolved))
                result['status'] = 'ready'
            else:
                result['resources'] = self.command('status', record, capture=True)
            present_result(result, self.args)
            return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, add_help=False, allow_abbrev=False)
    parser.add_argument('--graphx', type=Path, required=True)
    parser.add_argument('--source', type=Path)
    initial, rest = parser.parse_known_args(argv)
    # --source is accepted anywhere and points to a checkout with the existing builders.
    source = safe_path(initial.source or Path.cwd())
    while not (source / 'scripts/verify.sh').is_file() and source != source.parent and not initial.source:
        source = source.parent
    if not (source / 'scripts/verify.sh').is_file():
        raise WorkflowError('run from a GraphX checkout or provide --source CHECKOUT')
    cli = safe_path(initial.graphx)
    if not rest:
        parser.error('requires example, artifacts, env, verify or release')
    if rest[0] == 'artifacts':
        artifacts = argparse.ArgumentParser(prog='graphx artifacts', allow_abbrev=False)
        artifacts.add_argument('action', choices=['build'])
        artifacts.add_argument('--output', type=Path)
        artifacts.add_argument('--platform', choices=['linux/arm64', 'linux/amd64'])
        artifacts.add_argument('--fresh', action='store_true')
        args = artifacts.parse_args(rest[1:])
        source_key = hashlib.sha256(str(source).encode()).hexdigest()
        output = safe_path(args.output or Path.home() / '.cache/graphx/build' / source_key)
        machine = platform.machine().lower()
        image_platform = args.platform or (
            'linux/arm64' if machine in ('arm64', 'aarch64') else 'linux/amd64')
        command = [sys.executable, source / 'scripts/build_examples.py',
                   '--source', source, '--graphx', cli, '--output', output,
                   '--platform', image_platform]
        if args.fresh:
            command.append('--fresh')
        return subprocess.call(list(map(str, command)))
    if rest[0] == 'verify':
        return subprocess.call([str(source / 'scripts/verify.sh'), *rest[1:]])
    if rest[0] == 'release':
        tools = {'native': 'build_release.py', 'images': 'image_release.py', 'platform': 'platform_bundle.py',
                 'guests': 'guest_release.py', 'install': 'install_release.py', 'verify': 'verify_release.py',
                 'publish': 'ghcr_release.py'}
        if len(rest) < 2 or rest[1] not in tools:
            raise WorkflowError('release requires ' + ', '.join(tools))
        return subprocess.call([sys.executable, str(source / 'scripts/release' / tools[rest[1]]), *rest[2:]])
    if rest[0] == 'env':
        if rest[1:] == ['doctor']:
            for command in ([cli, '--version'], ['docker', 'info'], ['docker', 'compose', 'version']):
                run(command)
            if sys.platform == 'darwin':
                run(['limactl', 'list', 'graphx'])
            print('Environment checks passed')
            return 0
        if rest[1:] in (['up'], ['down']) and sys.platform == 'darwin':
            with lima_lock():
                return subprocess.call([str(source / 'infrastructure/lima' / ('start.sh' if rest[1] == 'up' else 'stop.sh'))])
        raise WorkflowError('env requires doctor, or up/down on macOS for explicit Lima lifecycle')
    examples = argparse.ArgumentParser(prog='graphx example', allow_abbrev=False)
    examples.add_argument('action', choices=['list', 'plan', 'prepare', 'up', 'status', 'tokens', 'open', 'logs', 'down', 'scenario'])
    examples.add_argument('name', nargs='?')
    examples.add_argument('--target', choices=['lima', 'orbstack', 'native-linux', 'native-macos'])
    examples.add_argument('--workspace', type=Path)
    examples.add_argument('--instance')
    examples.add_argument('--images', type=Path, help='existing verified images (guest path for Lima)')
    examples.add_argument('--catalog', type=Path, help='verified catalog override, e.g. a guest release catalog')
    examples.add_argument('--release', type=Path, help='existing verified installation (guest path for Lima)')
    examples.add_argument('--external', type=Path, help='external credential directory (guest path for Lima)')
    examples.add_argument('--laboratory')
    examples.add_argument('--control', action='append', default=[], metavar='NODE:ACTION,ACTION')
    examples.add_argument('--allow-privileged', action='store_true')
    examples.add_argument('--restart', action='store_true')
    examples.add_argument('--fresh-images', action='store_true',
                          help='build a new no-cache image release during prepare/up; requires --restart for an existing instance')
    examples.add_argument('--json', action='store_true')
    examples.add_argument('--no-open', action='store_true', help='do not open or authenticate a browser')
    examples.add_argument('--operator', help='existing control credential to use when opening the console')
    examples.add_argument('--node')
    examples.add_argument('--follow', action='store_true')
    examples.add_argument('--action', dest='scenario_action')
    examples.add_argument('--operation', choices=['plan', 'run', 'status', 'clear'], default='run')
    args = examples.parse_args(rest[1:])
    if args.fresh_images and (args.action not in ('prepare', 'up') or args.images or args.catalog or args.release):
        examples.error('--fresh-images requires prepare/up without --images, --catalog or --release')
    if args.action == 'list':
        rows = []
        for path in sorted((source / 'examples').rglob('graphx.yml')):
            targets = []
            for target in ([args.target] if args.target else ['native-linux', 'native-macos', 'orbstack', 'lima']):
                result = subprocess.run([cli, 'config', 'normalize', path, '--target', target],
                                        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=15)
                if result.returncode == 0:
                    targets.append(target)
            if targets:
                rows.append({'example': str(path.parent.relative_to(source / 'examples')), 'targets': targets})
        if args.json:
            print(json.dumps(rows))
        else:
            for row in rows:
                print(row['example'] + '  ' + ', '.join(row['targets']))
        return 0
    if not args.name:
        examples.error('NAME is required')
    workflow = Workflow(source, cli, args)
    if args.action == 'plan':
        run([cli, 'config', 'normalize', workflow.original, '--target', workflow.target], capture=True)
        grants(workflow.authored, args.control)
        emit({'example': args.name, 'target': workflow.target, 'privileged': workflow.privileged,
              'nodes': [n['node_id'] for n in workflow.normal['nodes']],
              'network_paths': workflow.normal['network']['edge_paths'],
              'preparation': ['verified images'] * workflow.containers + ['verified native release'] * workflow.native + ['verified guest artifacts'] * workflow.guests,
              'control': args.control}, args.json)
        return 0
    if sys.platform == 'darwin' and workflow.target == 'lima':
        return workflow.lima()
    return workflow.local()


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (WorkflowError, OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print('graphx example:', error, file=sys.stderr)
        raise SystemExit(2)
    except KeyboardInterrupt:
        print('Interrupted; use example status/down to inspect or clean up owned resources.', file=sys.stderr)
        raise SystemExit(130)
