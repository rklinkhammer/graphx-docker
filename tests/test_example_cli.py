#!/usr/bin/env python3
"""Workspace CLI selection, credential and reference boundaries; no infrastructure mutation."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

cli, root = map(lambda p: Path(p).resolve(), sys.argv[1:])
spec = importlib.util.spec_from_file_location('example_cli', root / 'scripts/example_cli.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def call(*args, ok=True):
    result = subprocess.run([cli, *map(str, args)], cwd=root, capture_output=True, text=True, timeout=30)
    assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
    return result


def rejected(fn):
    try:
        fn()
    except (module.WorkflowError, OSError, ValueError):
        return
    raise AssertionError('unsafe input accepted')


names = [row['example'] for row in json.loads(call('example', 'list', '--json').stdout)]
assert 'sample-pipeline/ovs' in names and 'sdr-node/simulated' in names
for name in names:
    plan = json.loads(call('example', 'plan', name, '--target', 'native-linux', '--json').stdout)
    assert plan['example'] == name and plan['target'] == 'native-linux'
assert 'E_TARGET_CAPABILITY' in call('example', 'plan', 'sample-pipeline/ovs', '--target', 'orbstack', ok=False).stderr
for name in ('../config', '/tmp', 'sample-pipeline/../../capture', 'sample pipeline'):
    call('example', 'plan', name, ok=False)
assert 'up' in call('example', '--help').stdout
original = json.loads(call('config', 'authored', root / 'examples/sample-pipeline/graphx.yml').stdout)
assert original['version'] == 3 and 'catalog_digest' not in original
changed = module.grants(original, ['generator:pause,resume'])
assert 'credentials' not in original and changed['credentials']['example-operator']['provider'] == 'external'
for grant in ('missing:pause', 'generator:destroy', 'generator:', 'generator'):
    rejected(lambda: module.grants(original, [grant]))
reserved = copy.deepcopy(changed)
rejected(lambda: module.grants(reserved, ['generator:pause']))

with tempfile.TemporaryDirectory(prefix='graphx-example-') as tmp:
    folder = Path(tmp).resolve()
    target = folder / 'target'
    target.mkdir()
    link = folder / 'link'
    link.symlink_to(target, target_is_directory=True)
    rejected(lambda: module.private_dir(link / 'nested'))
    fifo = folder / 'fifo'
    os.mkfifo(fifo)
    rejected(lambda: module.read_json(fifo))
    module.write_json(folder / 'record.json', {'generation': 'test'})
    assert (folder / 'record.json').stat().st_mode & 0o777 == 0o600
    assert module.read_json(folder / 'record.json') == {'generation': 'test'}
    args = SimpleNamespace(name='sample-pipeline', action='prepare', target='orbstack',
        workspace=folder, control=['generator:pause,resume'], images=None, release=None,
        external=None, catalog=None, laboratory=None, allow_privileged=False, instance=None, restart=False,
        json=True, follow=False, node=None, operation='run', scenario_action=None)
    workflow = module.Workflow(root, cli, args)
    workflow.folder = module.private_dir(folder / 'launch')
    selection = {'source': 'test', 'control': args.control, 'laboratory': None}
    with patch.object(workflow, 'artifacts', return_value=(None, None, root / 'config/catalog')):
        record = workflow.prepare(changed, selection)
    generation = workflow.generation(record)
    resolved = module.read_json(generation / 'compiled/resolved.json')
    assert resolved['platform']['control']['grants'][0]['nodes'] == ['generator']
    assert resolved['graph_id'].startswith('sample-pipeline-')
    assert 'http://127.0.0.1:8080' in resolved['platform']['control']['allowed_origins']
    token_file = generation / 'external/example-operator/token'
    token = token_file.read_text()
    assert len(token) >= 48 and token_file.stat().st_mode & 0o777 == 0o600
    assert token not in json.dumps(record)
    assert token not in (generation / 'input/graphx.yml').read_text()
    assert token not in (generation / 'compiled/credentials.json').read_text()
    forged = {**record, 'generation': '../foreign'}
    rejected(lambda: workflow.generation(forged))
    forged = {**record, 'privileged': True}
    rejected(lambda: workflow.generation(forged))
    # Both token classes are selected from the authoritative credential contract.
    with patch.object(workflow, 'command'), patch.object(module, 'run', return_value='test-token'):
        credentials = workflow.credentials(record, resolved)
    assert credentials['observation_token'] == credentials['control_token'] == 'test-token'
    without = copy.deepcopy(resolved)
    without['platform']['control']['enabled'] = False
    with patch.object(workflow, 'command'), patch.object(module, 'run', return_value='test-token'):
        assert workflow.credentials(record, without)['control_token'] is None
    workflow.target = 'lima'
    with patch.object(workflow, 'artifacts', return_value=(None, None, root / 'config/catalog')):
        lima_record = workflow.prepare(module.grants(original, ['generator:pause,resume']), selection)
    lima_resolved = module.read_json(workflow.generation(lima_record) / 'compiled/resolved.json')
    origins = lima_resolved['platform']['control']['allowed_origins']
    assert 'http://127.0.0.1:18080' in origins
    assert 'http://127.0.0.1:18081' not in origins and 'http://example.com:18080' not in origins
    # No command may bypass the existing explicit privileged authorization.
    workflow.privileged = True
    args.allow_privileged = False
    rejected(lambda: workflow.command('up', {**record, 'privileged': True}))
    # A rejected Lima identity must prevent transfer/build/remote execution.
    with patch.object(module, 'run', side_effect=module.WorkflowError('stale VM')), patch.object(module.subprocess, 'run') as remote:
        rejected(workflow.lima)
        remote.assert_not_called()
# Exercise the complete launch-reference state machine with a real compiler,
# replacing only artifact preparation and infrastructure execution.
with tempfile.TemporaryDirectory(prefix='graphx-example-state-') as tmp:
    args.workspace = Path(tmp).resolve()
    args.target, args.action, args.control, args.restart = 'native-linux', 'up', [], False
    args.images = args.release = args.external = args.catalog = None
    args.instance = None
    workflow = module.Workflow(root, cli, args)
    actions, active = [], [False]
    def lifecycle(action, record, capture=False):
        actions.append(action)
        if action == 'up': active[0] = True
        if action == 'down': active[0] = False
        return 'container application present' if active[0] else 'inactive graph=test'
    with patch.object(workflow, 'artifacts', return_value=(None, None, root / 'config/catalog')), \
         patch.object(workflow, 'command', side_effect=lifecycle), \
         patch.object(workflow, 'credentials', return_value={'observation_token': 'fixture', 'control_token': None}):
        workflow.local()
        first = module.read_json(workflow.folder / 'current.json')
        workflow.local()
        assert actions.count('up') == 1, actions
        args.control = ['generator:pause,resume']
        rejected(workflow.local)
        assert actions.count('up') == 1
        args.restart = True
        workflow.local()
        second = module.read_json(workflow.folder / 'current.json')
        assert second['generation'] != first['generation'] and actions.count('down') == 1
        args.action, args.restart = 'down', False
        workflow.local()
        assert not active[0]
        # The reference is durable before startup, so a failed/interrupted runner
        # can be cleaned up by a later command using the same generation.
        args.action = 'up'
        with patch.object(workflow, 'command', side_effect=module.WorkflowError('interrupted')):
            rejected(workflow.local)
        assert module.read_json(workflow.folder / 'current.json') == second
print('Example CLI: all authored plans, grants, private references, tokens and privilege/Lima boundaries passed')
