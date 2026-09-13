"""P1 resolved resource contracts; realization remains gated until P7/P8."""
import json
from pathlib import Path
import subprocess


def check(graphx, root, example):
    graphx, root = Path(graphx), Path(root)
    source = root / 'examples' / example / 'graphx.yml'
    result = subprocess.run([graphx, 'config', 'normalize', source], capture_output=True, text=True, timeout=15)
    assert result.returncode == 0, result.stderr
    value = json.loads(result.stdout)
    network = value['network']
    assert network['management_isolation']['ip_forward'] is False
    assert network['management_isolation']['platform_routes_data'] is False
    for switch in network['switches']:
        assert len(switch['name']) <= 15 and switch['name'].startswith('gxb')
    for attachment in network['attachments']:
        if attachment['kind'] != 'external':
            assert len(attachment['interface']) <= 15
    for action in ['create', 'status', 'destroy']:
        result = subprocess.run([graphx, 'infra', action, source, '--dry-run'], capture_output=True, text=True, timeout=15)
        assert result.returncode == 2 and not result.stdout and 'E_PHASE_UNAVAILABLE' in result.stderr
    return value
