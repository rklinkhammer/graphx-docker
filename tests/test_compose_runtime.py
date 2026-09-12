#!/usr/bin/env python3
"""Compose contract and failure-boundary checks without a Docker daemon."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

CLI, ROOT = map(lambda value: Path(value).resolve(), sys.argv[1:3])
sys.argv[1:] = []
spec = importlib.util.spec_from_file_location('instance', ROOT / 'scripts/instance.py')
instance = importlib.util.module_from_spec(spec)
spec.loader.exec_module(instance)


def normalize(path, *args):
    return json.loads(subprocess.check_output([CLI, 'config', 'normalize', path, *args],
        env={**os.environ, 'GRAPHX_OVERRIDES': ''}, text=True))


class ComposeRuntime(unittest.TestCase):
    def test_sample_and_sdr_use_authoritative_identity(self):
        for example, count in [('sample-pipeline', 3), ('sdr-node/two-source', 4)]:
            path = ROOT / 'examples' / example / 'graphx.yaml'
            config = normalize(path)
            resolved = normalize(path, '--resources')
            document = instance.render(config, resolved, Path('/runtime'), 28185)
            self.assertEqual(len(document['services']), count + (2 if example.startswith('sdr') else 1))
            self.assertEqual(document['name'], resolved['deployment']['project'])
            self.assertTrue(document['name'].startswith('gx'))
            for node in config['graph']['nodes']:
                service = document['services'][node['id']]
                self.assertEqual(service['environment']['GRAPHX_NODE_ID'], node['id'])
                secret_mounts = [m for m in service['volumes'] if m['target'] == '/run/node.secret']
                self.assertEqual(secret_mounts[0]['source'], f'/runtime/identity/{node["id"]}.secret')
                self.assertNotIn('/run/identity', [m['target'] for m in service['volumes']])
                self.assertNotIn('ports', service)
                if example.startswith('sdr'):
                    self.assertEqual(service['network_mode'], 'service:namespace')
            other = normalize(path, '--resources', '--set', 'deployment.instance_id=another')
            self.assertNotEqual(document['name'], other['deployment']['project'])

    def test_reject_unsupported_or_ambiguous_profiles_before_mutation(self):
        source = normalize(ROOT / 'examples/sdr-node/two-source/graphx.yaml')
        for change in ('instance', 'command', 'node', 'port', 'path', 'runtime'):
            value = copy.deepcopy(source)
            if change == 'instance':
                del value['deployment']['instance_id']
            elif change == 'command':
                value['deployment']['services'][0]['command'] = 'arbitrary-shell'
            elif change == 'node':
                value['deployment']['services'].pop()
            elif change == 'port':
                value['graph']['edges'][2]['transport']['port'] = 18400
            elif change == 'runtime':
                value['graph']['nodes'][0]['runtime'] = 'qemu'
            else:
                value['graph']['nodes'][0]['sdr']['credentials']['private_key_file'] = '/etc/key'
            with self.subTest(change=change), self.assertRaises(ValueError):
                instance.validate(value)

    def test_replaced_container_refuses_cleanup(self):
        runtime = object.__new__(instance.Runtime)
        runtime.digest = 'digest'
        runtime.project = 'project'
        runtime.inventory = lambda: [{'Id': 'replacement', 'Config': {'Labels': {}}}]
        with self.assertRaisesRegex(ValueError, 'replaced'):
            runtime.verify_receipt({'digest': 'digest', 'project': 'project', 'containers': {'original': {}}})

    def test_changed_registration_scope_prevents_restart_and_shutdown(self):
        with tempfile.TemporaryDirectory() as raw:
            runtime = object.__new__(instance.Runtime)
            runtime.directory = Path(raw)
            runtime.receipt = runtime.directory / 'containers.json'
            runtime.managed = False
            runtime.digest, runtime.project = 'digest', 'project'
            runtime.config = normalize(ROOT / 'examples/sample-pipeline/graphx.yaml')
            runtime.inventory = lambda: [{'Id': 'original', 'Config': {'Labels': {'org.graphx.node': 'generator'}}}]
            runtime.networks = lambda: []
            instance.write(runtime.receipt, {'digest': 'digest', 'project': 'project',
                'containers': {'original': {'org.graphx.node': 'generator'}}, 'networks': {},
                'executions': {'generator': '1' * 32}})
            (runtime.directory / 'identity').mkdir()
            instance.write(runtime.directory / 'identity/identities.json', {
                'version': 2, 'graph_id': 'sample-pipeline', 'instance_id': 'another',
                'nodes': [{'id': 'generator', 'secret_file': 'generator.secret', 'execution_id': '1' * 32}]})
            previous = instance.run
            def forbidden(*args, **kwargs):
                raise AssertionError('mutation occurred before scope validation')
            instance.run = forbidden
            try:
                for operation in (lambda: runtime.restart('generator'), runtime.down):
                    with self.assertRaisesRegex(ValueError, 'scope'):
                        operation()
            finally:
                instance.run = previous

    def test_failure_before_ovs_creation_does_not_destroy_infrastructure(self):
        with tempfile.TemporaryDirectory() as raw:
            runtime = object.__new__(instance.Runtime)
            runtime.directory = Path(raw)
            runtime.receipt = runtime.directory / 'containers.json'
            runtime.managed = True
            runtime.digest, runtime.project = 'digest', 'project'
            runtime.config = normalize(ROOT / 'examples/sample-pipeline/graphx.yaml')
            runtime.inventory, runtime.networks = lambda: [], lambda: []
            instance.write(runtime.receipt, {'digest': 'digest', 'project': 'project',
                'containers': {}, 'networks': {}, 'executions': {}, 'infra_attempted': False})
            (runtime.directory / 'identity').mkdir()
            instance.write(runtime.directory / 'identity/identities.json', {
                'version': 2, 'graph_id': 'sample-pipeline', 'instance_id': 'demo', 'nodes': []})
            previous = instance.run
            def forbidden(*args, **kwargs):
                raise AssertionError('uncreated infrastructure was mutated')
            instance.run = forbidden
            try:
                runtime.down()
                self.assertFalse(runtime.receipt.exists())
            finally:
                instance.run = previous

    def test_protected_artifacts_reject_symlinks_and_hardlinks(self):
        with tempfile.TemporaryDirectory() as raw:
            directory = Path(raw)
            target = directory / 'file'
            instance.write(target, {'safe': True})
            self.assertEqual(instance.load(target), {'safe': True})
            link = directory / 'link'
            link.symlink_to(target)
            with self.assertRaises(OSError):
                instance.load(link)
            link.unlink()
            os.link(target, link)
            with self.assertRaises(ValueError):
                instance.load(target)

    def test_generated_sdr_certificates_pass_strict_verification(self):
        config = normalize(ROOT / 'examples/sdr-node/two-source/graphx.yaml')
        with tempfile.TemporaryDirectory() as raw:
            directory = Path(raw)
            instance.provision_tls(config, directory)
            for side in ('east', 'west'):
                ca = directory / 'tls' / ('sdr-' + side) / 'ca.pem'
                for certificate in (directory / 'tls' / ('sdr-' + side) / 'server.pem',
                                    directory / 'tls' / ('processor-' + side) / 'client.pem'):
                    subprocess.run(['openssl', 'verify', '-x509_strict', '-CAfile', ca, certificate],
                                   check=True, capture_output=True)
            rejected = subprocess.run(['openssl', 'verify', '-CAfile',
                directory / 'tls/sdr-east/ca.pem', directory / 'tls/sdr-west/server.pem'],
                capture_output=True)
            self.assertNotEqual(rejected.returncode, 0, 'source pairs share a CA')

    def test_execution_gate_cannot_be_restarted(self):
        with tempfile.TemporaryDirectory() as raw:
            gate = Path(raw)
            (gate / 'ready').touch()
            script = instance.GATE.replace('/gate', str(gate))
            command = ['/bin/sh', '-c', script, 'test', '/bin/echo', 'activated']
            first = subprocess.run(command, capture_output=True, text=True)
            second = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(first.returncode, 0)
            self.assertIn('activated', first.stdout)
            self.assertNotEqual(second.returncode, 0)
            self.assertNotIn('activated', second.stdout)


if __name__ == '__main__':
    unittest.main()
