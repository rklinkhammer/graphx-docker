"""Bounded file projections of resolved credential references; no provisioning."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import stat

_generations = {}


def protected(path: Path, maximum: int = 65536) -> bytes:
    for parent in (path, *path.parents):
        if parent.is_symlink():
            raise ValueError('credential paths must not contain symlinks')
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        metadata = os.fstat(descriptor)
        if not stat.S_ISREG(metadata.st_mode) or metadata.st_nlink != 1 or metadata.st_mode & 0o077:
            raise ValueError('credential must be a private regular file')
        value = os.read(descriptor, maximum + 1)
        if len(value) > maximum:
            raise ValueError('credential exceeds file bound')
        return value
    finally:
        os.close(descriptor)


def member(path: Path) -> bytes:
    generation = path.parent / 'generation.json'
    before = protected(generation, 8192)
    metadata = json.loads(before)
    known = _generations.get(generation)
    number = metadata.get('generation')
    if type(number) is not int or number < 1 or (known and (number < known[0] or (number == known[0] and before != known[1]))):
        raise ValueError('stale credential generation')
    value = protected(path)
    if metadata.get('version') != 1 or metadata.get('members', {}).get(path.name) != hashlib.sha256(value).hexdigest() or before != protected(generation, 8192):
        raise ValueError('credential generation is incomplete')
    _generations[generation] = (number, before)
    return value


def configure_tls(node: dict) -> None:
    root = os.environ.get('GRAPHX_CREDENTIALS')
    if not root:
        return
    ref = node['credentials'].get('control')
    if not ref:
        return
    directory = Path(root) / ref
    for name in ('ca.pem', 'cert.pem', 'key.pem'):
        member(directory / name)
    for key, name in {'SDR_TLS_CA': 'ca.pem', 'SDR_TLS_CLIENT_CA': 'ca.pem',
                      'SDR_TLS_CERT': 'cert.pem', 'SDR_TLS_KEY': 'key.pem'}.items():
        os.environ[key] = str(directory / name)


def tls_context(server: bool, files=None):
    """Load one consistent reference generation for a new TLS session."""
    import ssl
    files = os.environ if files is None else files
    directory = Path(files['SDR_TLS_CERT']).parent
    for _ in range(3):
        before = protected(directory / 'generation.json', 8192) if files.get('GRAPHX_CREDENTIALS') else None
        if before is not None:
            for name in ('ca.pem', 'cert.pem', 'key.pem'):
                member(directory / name)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER if server else ssl.PROTOCOL_TLS_CLIENT)
        context.minimum_version = ssl.TLSVersion.TLSv1_3
        context.verify_mode = ssl.CERT_REQUIRED
        context.load_verify_locations(files['SDR_TLS_CLIENT_CA' if server else 'SDR_TLS_CA'])
        context.load_cert_chain(files['SDR_TLS_CERT'], files['SDR_TLS_KEY'])
        if before is None or before == protected(directory / 'generation.json', 8192):
            return context
    raise ValueError('TLS credential generation changed while loading')
