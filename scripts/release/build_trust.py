"""Optional organization trust for Docker builds; never execute host installers."""
import hashlib
import os
from pathlib import Path


def build_trust(environment=None):
    """Return a content fingerprint and BuildKit arguments, matching the shell helper."""
    environment = os.environ if environment is None else environment
    material = ''
    arguments = []
    for variable, secret in (('GRAPHX_CA_CERT', 'graphx_ca'),
                             ('GRAPHX_CERT_INSTALL_SCRIPT', 'graphx_cert_installer')):
        value = environment.get(variable, '')
        if not value:
            continue
        path = Path(value)
        if not path.is_absolute() or not path.is_file():
            raise ValueError(f'{variable} must be an absolute readable regular file')
        # BuildKit parses secret specifications as CSV; do not allow another field.
        if any(character in value for character in ',\r\n'):
            raise ValueError(f'{variable} must not contain commas or newlines')
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        material += f'{variable}={digest};'
        arguments += ['--secret', f'id={secret},src={value}']
    fingerprint = ('graphx-trust-v1-' + hashlib.sha256(material.encode()).hexdigest()
                   if material else 'graphx-trust-v1-none')
    return fingerprint, ['--build-arg', 'GRAPHX_BUILD_TRUST_FINGERPRINT=' + fingerprint, *arguments]
