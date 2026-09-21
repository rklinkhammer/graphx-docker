#!/usr/bin/env python3
"""Workspace CLI selection, credential and reference boundaries; no infrastructure mutation."""
import copy
import importlib.util
import json
import io
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


def call(*args, ok=True, timeout=30):
    result = subprocess.run([cli, *map(str, args)], cwd=root, capture_output=True, text=True, timeout=timeout)
    assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
    return result


def rejected(fn):
    try:
        fn()
    except (module.WorkflowError, OSError, ValueError):
        return
    raise AssertionError('unsafe input accepted')


# Listing validates every example against all four targets, including in Debug
# and sanitizer builds; give this aggregate operation its own bounded deadline.
names = [row['example'] for row in json.loads(call('example', 'list', '--json', timeout=120).stdout)]
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
changed = module.grants(original, ['generator:pause,resume', 'collector:reset'])
assert 'credentials' not in original and changed['credentials']['example-operator']['provider'] == 'external'
for grant in ('missing:pause', 'generator:destroy', 'generator:', 'generator', 'generator:reset', 'collector:pause', 'collector:reset,resume'):
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
    assert resolved['platform']['control']['grants'][1]['nodes'] == ['collector']
    assert resolved['platform']['control']['grants'][1]['actions'] == ['reset']
    # The authoritative loader also rejects invalid scopes without CLI prevalidation.
    for nodes, actions in [(['generator'], ['reset']), (['collector'], ['pause']),
                           (['collector', 'generator'], ['reset']), (['missing'], ['reset'])]:
        invalid = copy.deepcopy(changed)
        invalid['platform']['control']['grants'][1].update(nodes=nodes, actions=actions)
        authored_path = folder / 'invalid-grant.json'
        invalid['catalog'] = os.path.relpath(root / 'config/catalog/lock.json', authored_path.parent)
        authored_path.write_text(json.dumps(invalid))
        result = call('config', 'normalize', authored_path, '--target', 'orbstack',
                      '--catalog-root', root / 'config/catalog', ok=False)
        assert 'E_REFERENCE' in result.stderr, result.stderr
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
        args.action = 'open'
        workflow.local()
        assert actions.count('up') == 1
        args.action = 'up'
        args.control = ['generator:pause,resume']
        rejected(workflow.local)
        assert actions.count('up') == 1
        args.restart = True
        workflow.local()
        second = module.read_json(workflow.folder / 'current.json')
        assert second['generation'] != first['generation'] and actions.count('down') == 1
        args.fresh_images, args.restart = True, False
        rejected(workflow.local)
        assert actions.count('down') == 1
        args.restart = True
        workflow.local()
        fresh_record = module.read_json(workflow.folder / 'current.json')
        assert fresh_record['generation'] != second['generation']
        second = fresh_record
        args.fresh_images = False
        workflow.local()
        assert module.read_json(workflow.folder / 'current.json') == second
        # A failed fresh release preserves the previous stopped generation for cleanup.
        args.fresh_images = True
        with patch.object(workflow, 'artifacts', side_effect=module.WorkflowError('build interrupted')):
            rejected(workflow.local)
        assert module.read_json(workflow.folder / 'current.json') == second
        assert not active[0]
        args.fresh_images = False
        args.action, args.restart = 'down', False
        workflow.local()
        assert not active[0]
        # The reference is durable before startup, so a failed/interrupted runner
        # can be cleaned up by a later command using the same generation.
        args.action = 'up'
        with patch.object(workflow, 'command', side_effect=module.WorkflowError('interrupted')):
            rejected(workflow.local)
        assert module.read_json(workflow.folder / 'current.json') == second
# Browser handoff uses authenticated POST, no redirect/proxy, and no token in its URL.
credentials = {'console': 'http://127.0.0.1:18080', 'observation_token': 'observation-secret',
               'control_token': 'control-secret', 'control_scope': []}
code = 'a' * 43
opener = SimpleNamespace(open=lambda request, timeout: io.BytesIO(json.dumps({'code': code}).encode()))
requests = []
def exchange(request, timeout):
    requests.append(request)
    assert request.get_header('Authorization') == 'Bearer observation-secret'
    assert json.loads(request.data)['control_token'] == 'control-secret'
    assert request.get_header('Origin') == credentials['console']
    return io.BytesIO(json.dumps({'code': code}).encode())
with patch.object(module.urllib.request, 'build_opener', return_value=SimpleNamespace(open=exchange)), \
     patch.object(module.webbrowser, 'open', return_value=True) as browser:
    module.open_console(credentials)
    browser.assert_called_once_with(credentials['console'] + '/#graphx-login=' + code)
    assert len(requests) == 1
for console in ('http://evil.test', 'file:///tmp/console', 'http://user@127.0.0.1:8080'):
    rejected(lambda: module.open_console({**credentials, 'console': console}))
with patch.object(module, 'open_console') as browser, patch.object(module, 'emit') as emit:
    options = SimpleNamespace(action='up', json=False, no_open=False, operator=None)
    module.present_result(credentials, options)
    browser.assert_called_once()
    assert 'control_token' not in emit.call_args.args[0]
    assert 'observation_token' not in emit.call_args.args[0]
    for flag in ('json', 'no_open'):
        browser.reset_mock()
        setattr(options, flag, True)
        module.present_result(credentials, options)
        browser.assert_not_called()
        setattr(options, flag, False)
print('Example CLI: all authored plans, grants, private references, tokens and privilege/Lima boundaries passed')

# Optional VITA role selection cannot reuse a default-role cache or accept fault hooks.
with tempfile.TemporaryDirectory(prefix='graphx-vita-artifacts-') as directory:
    workspace = Path(directory).resolve()
    module.private_dir(workspace / 'artifacts/key')
    (workspace / 'artifacts/key/images').mkdir()
    workflow = module.Workflow.__new__(module.Workflow)
    workflow.base, workflow.key, workflow.source = workspace, 'key', root
    workflow.containers, workflow.guests, workflow.native = True, False, False
    workflow.normal = {'nodes': [{'type': 'vita.radio'}]}
    workflow.args = SimpleNamespace(images=None, release=None, catalog=None)
    commands = []
    def artifact_run(command, **kwargs):
        commands.append(command)
        if 'build' in command:
            destination = command[command.index('--output') + 1]
            destination.mkdir()
            module.write_json(destination / 'images.json', {'images': {'vita': {}}, 'qualification_hooks': False})
        return ''
    with patch.object(module, 'run', side_effect=artifact_run):
        images, _, _ = workflow.artifacts()
        assert images.name == 'images-vita'
        build = next(c for c in commands if 'build' in c)
        assert '--with-vita' in build and '--no-cache' in build and '--qualification-hooks' not in build
        for manifest in ({'images': {}, 'qualification_hooks': False},
                         {'images': {'vita': {}}, 'qualification_hooks': True}):
            module.write_json(images / 'images.json', manifest)
            rejected(workflow.artifacts)
        workflow.args.fresh_images = True
        fresh_images, _, _ = workflow.artifacts()
        newer_images, _, _ = workflow.artifacts()
        assert images != fresh_images != newer_images
        assert sum('build' in command for command in commands) == 3

# Host environment lifecycle is mocked: portable tests never create a VM.
with tempfile.TemporaryDirectory(prefix='graphx-lima-lifecycle-') as temporary:
    home = Path(temporary).resolve()
    with patch.object(module.Path, 'home', return_value=home):
        with module.lima_lock():
            rejected(lambda: module.lima_lock().__enter__())
        (home / '.graphx/lima.lock').unlink()
        (home / '.graphx/lima.lock').symlink_to(home / 'foreign')
        rejected(lambda: module.lima_lock().__enter__())

workflow = object.__new__(module.Workflow)
workflow.source = root
workflow.privileged = True
for action in ('up', 'prepare', 'down', 'status'):
    workflow.args = SimpleNamespace(action=action, allow_privileged=True)
    with patch.object(module, 'lima_lock', module.contextlib.nullcontext), \
         patch.object(module, 'run', return_value='Running') as command, \
         patch.object(workflow, 'lima_dispatch', return_value=0) as dispatch, \
         patch.object(workflow, 'lima_stop_if_idle') as stop:
        assert workflow.lima() == 0
        assert dispatch.call_count == 1
        assert stop.call_count == (1 if action in ('prepare', 'down') else 0)
        assert ('start.sh' in str(command.call_args)) == (action in ('prepare', 'up'))
workflow.args = SimpleNamespace(action='up', allow_privileged=False)
with patch.object(module, 'run') as command:
    rejected(workflow.lima)
    command.assert_not_called()
workflow.args.action = 'prepare'
with patch.object(module, 'run') as command:
    rejected(workflow.lima)
    command.assert_not_called()
workflow.args.action = 'up'
workflow.args.allow_privileged = True
with patch.object(module, 'lima_lock', module.contextlib.nullcontext), \
     patch.object(module, 'run', side_effect=module.WorkflowError('foreign VM')), \
     patch.object(workflow, 'lima_dispatch') as dispatch:
    rejected(workflow.lima)
    dispatch.assert_not_called()
workflow.args.action = 'down'
with patch.object(module, 'lima_lock', module.contextlib.nullcontext), \
     patch.object(module, 'run', return_value='Running'), \
     patch.object(workflow, 'lima_dispatch', return_value=1), \
     patch.object(workflow, 'lima_stop_if_idle') as stop:
    assert workflow.lima() == 1
    stop.assert_not_called()
for result in (0, 1, 2):
    with patch.object(module.subprocess, 'run', return_value=SimpleNamespace(returncode=result)), \
         patch.object(module, 'run') as command:
        workflow.lima_stop_if_idle()
        assert command.call_count == (1 if result == 0 else 0)
with patch.object(module.subprocess, 'run', side_effect=subprocess.TimeoutExpired('probe', 45)), \
     patch.object(module, 'run') as command:
    workflow.lima_stop_if_idle()
    command.assert_not_called()

idle_spec = importlib.util.spec_from_file_location('lima_idle', root / 'infrastructure/lima/idle.py')
idle_module = importlib.util.module_from_spec(idle_spec)
idle_spec.loader.exec_module(idle_module)
with tempfile.TemporaryDirectory(prefix='graphx-idle-proc-') as temporary:
    proc = Path(temporary)
    empty = ['', '', '', '[]', '', '[]']
    with patch.object(idle_module, 'output', side_effect=empty):
        assert idle_module.idle(proc)
    for inventory in (['container'], ['', 'bridge'], ['', '', 'namespace'],
                      ['', '', '', '[{"linkinfo":{"info_kind":"veth"}}]'],
                      ['', '', '', '[]', 'table inet graphx_test'],
                      ['', '', '', '[]', '', '[{"kind":"netem"}]']):
        with patch.object(idle_module, 'output', side_effect=inventory):
            assert not idle_module.idle(proc)
    process = proc / '999999'
    process.mkdir()
    (process / 'cmdline').write_bytes(b'node\0/var/lib/graphx/example.js\0')
    (process / 'exe').symlink_to('/usr/bin/node')
    with patch.object(idle_module, 'output', side_effect=empty):
        assert not idle_module.idle(proc)
print('Lima automatic lifecycle checks passed')

# Reject contradictory fresh-build selections before touching any infrastructure.
for flags in (['--images', '/missing'], ['--release', '/missing'], ['--catalog', '/missing']):
    result = call('example', 'prepare', 'four-radio-vita', '--fresh-images', *flags, ok=False)
    assert '--fresh-images requires' in result.stderr
assert '--fresh-images requires' in call('example', 'down', 'four-radio-vita', '--fresh-images', ok=False).stderr
