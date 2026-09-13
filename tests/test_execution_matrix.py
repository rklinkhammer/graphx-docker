#!/usr/bin/env python3
"""Selected-engine P6 matrix with a separately owned workload preserved throughout."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import uuid

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('images', type=Path)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
args.images = args.images.resolve()
args.output = args.output.resolve()
root = Path(__file__).resolve().parents[1]
args.output.mkdir(parents=True, exist_ok=False)
subprocess.run(['docker', 'info'], check=True, stdout=subprocess.DEVNULL)
subprocess.run(['docker', 'compose', 'version'], check=True)
if sys.platform == 'darwin':
    assert subprocess.check_output(['docker', 'context', 'show'], text=True).strip() == 'orbstack'
manifest = json.loads((args.images / 'images.json').read_text())
image = manifest['images']['runtime']['inspection']['config_digest']
# Verify archive bytes before loading the image used for our unrelated sentinel.
import hashlib
assert hashlib.sha256((args.images/'runtime.oci.tar').read_bytes()).hexdigest() == manifest['images']['runtime']['archive_sha256']
subprocess.run(['docker', 'image', 'load', '--input', str(args.images/'runtime.oci.tar')], check=True)
owner = uuid.uuid4().hex
sentinel = subprocess.check_output(['docker', 'run', '-d', '--name', 'graphx-p6-sentinel-'+owner,
    '--label', 'org.graphx.test='+owner, '--network', 'none', '--read-only', '--user', '65532:65532',
    '--cap-drop', 'ALL', '--security-opt', 'no-new-privileges:true', '--entrypoint', '/bin/sleep', image, '900'], text=True).strip()
cases = [('S01', 'examples/sample-pipeline/graphx.yml'), ('S05', 'examples/udp-broadcast/graphx.yml'),
    ('S13', 'examples/sdr-node/simulated/graphx.yml')]
cases += [(f'V{i:02}', f'examples/variants/{name}/graphx.yml') for i, name in enumerate(
    ['history', 'observability', 'control', 'credential-rotation', 'secure-otlp', 'otlp-mtls'], 1)]
cases += [('T01', 'examples/variants/renamed-multi-source/graphx.yml'), ('T02', 'examples/variants/multi-radio/graphx.yml')]
results = []
try:
    for case, authored in cases:
        print(case, authored, flush=True)
        with (args.output/(case+'.log')).open('w') as log:
            subprocess.run([sys.executable, root/'tests/test_execution_docker.py', args.images,
                '--output', args.output/case, '--case', authored], stdout=log, stderr=subprocess.STDOUT, check=True, cwd=root)
        metadata = json.loads(subprocess.check_output(['docker', 'inspect', sentinel], text=True))[0]
        assert metadata['State']['Running'] and metadata['Config']['Labels']['org.graphx.test'] == owner
        results.append({'case':case, 'result':'pass', 'sentinel_running':True})
        print(case, 'PASS; unrelated sentinel running', flush=True)
finally:
    metadata = json.loads(subprocess.check_output(['docker', 'inspect', sentinel], text=True))[0]
    assert metadata['Id'] == sentinel and metadata['Config']['Labels']['org.graphx.test'] == owner
    subprocess.run(['docker', 'stop', '--time', '1', sentinel], check=True)
    subprocess.run(['docker', 'rm', sentinel], check=True)
    (args.output/'results.json').write_text(json.dumps(results, indent=2)+'\n')
