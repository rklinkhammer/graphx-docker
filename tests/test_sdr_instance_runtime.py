#!/usr/bin/env python3
"""Run two typed SDR sources concurrently using their registered node identities."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile


def main():
    cli, root = map(lambda value: Path(value).resolve(), sys.argv[1:])
    common = root / 'examples/sdr-node/common'
    sys.path.insert(0, str(common))
    from protocol import decode_samples, verified
    environment = {**os.environ, 'GRAPHX_OVERRIDES': '', 'GRAPHX_TELEMETRY_SHARED_SECRET': ''}
    with tempfile.TemporaryDirectory(prefix='graphx-sdr-instances-') as raw:
        directory = Path(raw)
        tls = directory / 'tls'
        subprocess.run([common / 'generate_tls.sh', tls], check=True, capture_output=True)
        sockets = []
        children = []
        try:
            for _ in range(3):
                endpoint = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                endpoint.bind(('127.0.0.1', 0))
                endpoint.settimeout(5)
                sockets.append(endpoint)
            ports = [endpoint.getsockname()[1] for endpoint in sockets]
            controls = []
            for _ in range(2):
                with socket.socket() as endpoint:
                    endpoint.bind(('127.0.0.1', 0))
                    controls.append(endpoint.getsockname()[1])
            source = (root / 'examples/sdr-node/two-source/graphx.yaml').read_text()
            for old, new in [(18400, ports[0]), (18410, ports[1]), (18401, controls[0]), (18411, controls[1])]:
                source = source.replace(f'port: {old}', f'port: {new}')
            for side in ('east', 'west'):
                for name, filename in [('ca.pem', 'ca.pem'), ('server.pem', 'sdr-node.pem'),
                                       ('server.key', 'sdr-node.key')]:
                    source = source.replace(f'/run/sdr-{side}/{name}', str(tls / filename))
                source = source.replace(f'server_name: sdr-{side}', 'server_name: sdr-node')
            source += f'\nobservability:\n  telemetry: {{host: 127.0.0.1, port: {ports[2]}}}\n'
            config = directory / 'graphx.yaml'
            config.write_text(source)
            normalized = directory / 'normalized.json'
            normalized.write_bytes(subprocess.check_output([cli, 'config', 'normalize', config], env=environment))
            value = json.loads(normalized.read_text())
            identities = directory / 'identities.json'
            identities.write_text(json.dumps({'version': 2, 'graph_id': value['graph']['id'],
                'instance_id': 'lab-a', 'nodes': [{'id': node['id'], 'secret_file': node['id'] + '.secret'}
                                                for node in value['graph']['nodes']]}))
            secrets, executions = {}, {}
            for side in ('east', 'west'):
                node = f'sdr-{side}'
                secret = f'{node}-unique-telemetry-credential-for-portable-test'
                secret_file = directory / f'{node}.secret'
                secret_file.write_text(secret)
                execution = subprocess.check_output([cli, 'runtime', 'activate', config,
                    '--identity-file', identities, '--node', node], text=True, env=environment).strip()
                secrets[node], executions[node] = secret, execution
                child = subprocess.Popen([sys.executable, common / 'sdr_simulator.py'],
                    env={**environment, 'GRAPHX_NORMALIZED_CONFIG': str(normalized), 'GRAPHX_NODE_ID': node,
                         'GRAPHX_EXECUTION_ID': execution, 'GRAPHX_TELEMETRY_SHARED_SECRET_FILE': str(secret_file)},
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                children.append(child)
            for index, frequency in enumerate((100_000_000, 200_000_000)):
                packet, _ = sockets[index].recvfrom(2048)
                assert decode_samples(packet)[1] == frequency, 'source settings crossed node boundary'
            observed = set()
            for _ in range(8):
                packet, _ = sockets[2].recvfrom(16384)
                node = json.loads(packet)['payload']['nodeId']
                event = verified(packet, secrets[node])
                assert event['instanceId'] == 'lab-a' and event['executionId'] == executions[node]
                observed.add(node)
                if len(observed) == 2:
                    break
            assert observed == {'sdr-east', 'sdr-west'}
        finally:
            for child in children:
                child.terminate()
                try:
                    stdout, stderr = child.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    child.kill()
                    stdout, stderr = child.communicate()
                if child.returncode not in (0, -15):
                    print(stdout.decode(), stderr.decode(), file=sys.stderr)
            for endpoint in sockets:
                endpoint.close()
    print('Two typed SDR source runtimes passed')


if __name__ == '__main__':
    main()
