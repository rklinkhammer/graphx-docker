# Compatibility examples

`v1/` contains the curated configuration-version-1 inputs retained by M8.
They exist only to validate, inspect, project, and deterministically migrate old
topologies. GraphX no longer realizes infrastructure from version 1.

```sh
graphx validate examples/compatibility/v1/macvlan.yaml
graphx config migrate examples/compatibility/v1/macvlan.yaml \
  --output /tmp/macvlan-v2.yaml
graphx validate /tmp/macvlan-v2.yaml
graphx infra create /tmp/macvlan-v2.yaml --dry-run
```

Review the generated semantic profiles, OVS switches, and veth/TAP attachments
before using the migrated file. See [`../../docs/m8-compatibility.md`](../../docs/m8-compatibility.md).
