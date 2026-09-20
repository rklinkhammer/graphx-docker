#!/usr/bin/env python3
"""Emit reproducible SPDX dependency inputs for the opt-in standalone radio."""
import hashlib
import json
from pathlib import Path
import sys

lock = Path(sys.argv[1]).read_bytes()
packages = []
for dependency in json.loads(lock)['dependencies']:
    package = {
        'name': dependency['name'], 'SPDXID': 'SPDXRef-' + dependency['name'].replace('_', '-'),
        'versionInfo': dependency['revision'],
        'downloadLocation': dependency.get('source',
            'https://github.com/pothosware/SoapySDR/tree/' + dependency['revision']),
        'filesAnalyzed': False, 'licenseConcluded': dependency['license'],
        'licenseDeclared': dependency['license'], 'copyrightText': 'NOASSERTION',
    }
    if 'sha256' in dependency:
        package['checksums'] = [{'algorithm': 'SHA256', 'checksumValue': dependency['sha256']}]
    packages.append(package)
document = {
    'spdxVersion': 'SPDX-2.3', 'dataLicense': 'CC0-1.0', 'SPDXID': 'SPDXRef-DOCUMENT',
    'name': 'GraphX standalone VITA dependencies',
    'documentNamespace': 'https://graphx.invalid/spdx/vita/' + hashlib.sha256(lock).hexdigest(),
    'creationInfo': {'creators': ['Tool: GraphX dependency_sbom.py'],
                     'created': '2026-09-20T00:00:00Z'},
    'documentComment': 'Dependency inputs only; not a qualified release or complete runtime SBOM.',
    'packages': packages,
    'relationships': [{'spdxElementId': 'SPDXRef-DOCUMENT', 'relationshipType': 'DESCRIBES',
                       'relatedSpdxElement': package['SPDXID']} for package in packages],
}
Path(sys.argv[2]).write_text(json.dumps(document, indent=2) + '\n')
