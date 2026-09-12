# Instance-aware sample pipeline

The three-node TCP pipeline runs on Linux Docker Compose or macOS OrbStack.
The [shared instance launcher](../../docs/compose-runtime.md) supplies service
policy, per-node credentials, execution registration, history, and capture storage.

```sh
python3 scripts/instance.py up examples/sample-pipeline/graphx.yaml --build --port 28080
python3 scripts/instance.py token examples/sample-pipeline/graphx.yaml
python3 scripts/instance.py status examples/sample-pipeline/graphx.yaml
python3 scripts/instance.py down examples/sample-pipeline/graphx.yaml
```

The default instance is `demo`. Pass `--set deployment.instance_id=lab-b` and a
different published port to select another deployment; repeat that instance
selection on every lifecycle command. Captures/history survive shutdown.

`ovs/graphx.yaml` supplies the alternative owned container-veth/system-OVS data
plane, with instance `ovs-demo`. Run it with explicit root authorization on Linux
or inside the GraphX Lima guest; it cannot run on native macOS or OrbStack.
See the shared runtime guide for the exact Lima command and lifecycle boundaries.
