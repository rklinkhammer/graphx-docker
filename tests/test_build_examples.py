#!/usr/bin/env python3
"""All-example orchestration boundaries with real graph compilation, no containers."""
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
from types import SimpleNamespace
from unittest.mock import patch

root, cli = map(lambda p: Path(p).resolve(), sys.argv[1:])
sys.path.insert(0, str(root / 'scripts'))
spec = importlib.util.spec_from_file_location('build_examples', root / 'scripts/build_examples.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def rejected(function):
    try:
        function()
    except (module.WorkflowError, OSError):
        return
    raise AssertionError('unsafe build accepted')


with tempfile.TemporaryDirectory(prefix='graphx-build-examples-') as temporary:
    output = Path(temporary).resolve() / 'artifacts'
    args = SimpleNamespace(source=root, graphx=cli, output=output, platform='linux/arm64', fresh=False)
    commands = []
    original = module.run
    def fake_run(*command, **options):
        commands.append(list(map(str, command)))
        if str(command[0]) == sys.executable:
            destination = Path(command[command.index('--output') + 1])
            destination.mkdir(parents=True)
            if str(command[1]).endswith(('image_release.py', 'guest_release.py')):
                shutil.copytree(root / 'config/catalog', destination / 'catalog')
            return None
        return original(*command, **options)
    with patch.object(module, 'run', side_effect=fake_run), patch.object(module, 'verify') as verify:
        module.build(args)
        first = module.read_json(output / 'current.json')
        inventory = module.read_json(output / first['generation'] / 'compiled.json')
        assert len([p for p in inventory if p.endswith('compile-manifest.json')]) == 26
        builds = [c for c in commands if c[0] == sys.executable]
        assert len(builds) == 3 and '--with-vita' in builds[0] and '--no-cache' in builds[0]
        commands.clear()
        module.build(args)
        assert not any(c[0] == sys.executable for c in commands)
        verify.assert_called()
        args.fresh = True
        with patch.object(module, 'run', side_effect=module.WorkflowError('interrupted build')):
            rejected(lambda: module.build(args))
        assert module.read_json(output / 'current.json') == first
        args.fresh = False
        module.write_json(output / 'current.json', {'source_digest': first['source_digest'], 'generation': '../foreign'})
        rejected(lambda: module.build(args))
    args.output = root / 'outputs/forbidden-build-fixture'
    rejected(lambda: module.build(args))
    args.output = output
    module.write_json(output / 'owner.json', {'source': '/foreign'})
    rejected(lambda: module.build(args))
print('All-example build: 26 graphs, shared stages, reuse and ownership boundaries passed')
