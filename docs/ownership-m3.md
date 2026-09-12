# GraphX M3 OVS ownership lifecycle

M3 is the first version-2 realization boundary. It creates and owns only the
declared Open vSwitch bridges. Endpoint ports, veth pairs, namespaces, mirrors,
routes, faults, captures, and TAP devices remain later milestones.

## State and identity

The default state root is `/var/lib/graphx/runs`; use `--state-dir DIR` for a
bounded test location. The directory is mode 0700 and each per-graph YAML state
file is mode 0600. `GRAPHX_STATE_DIR` may set the default. A non-blocking
per-graph file lock prevents concurrent mutation. The zero-length mode-0600
lock file remains after destroy so every later operation serializes on the same
inode; it is synchronization metadata, not an ownership ledger or live run.

Before mutation, GraphX records the graph ID, SHA-256 of the literal
configuration, a cryptographically random owner token, lifecycle status, and
every expected bridge. Each bridge is created in one OVSDB transaction with the
system datapath and these `external_ids`:

- `graphx_owner`: per-run random token;
- `graphx_config_hash`: literal configuration SHA-256;
- `graphx_graph`: graph ID.

The initial state is published atomically without replacing any existing
directory entry, including a dangling symlink. Later owner-only replacement
records the stable OVS UUID after every mutation and makes interrupted state
reviewable. The state model is
the foundation for later ifindex, namespace inode, container identity, TAP
owner, route, rule, qdisc, capture, and process identity records; M3 does not
invent records for resources it does not create.

## Commands

```text
graphx infra create CONFIG --state-dir DIR
graphx infra status CONFIG --state-dir DIR
graphx infra destroy CONFIG --state-dir DIR
graphx infra recover CONFIG --state-dir DIR
```

Version-2 create is always transactional. Dry-run prints the state path,
configuration digest, placeholder token, and OVS mutations without writing
state. Create refuses an existing state file or any declared bridge name that
already exists.

Status is read-only: it neither creates the state root nor creates or changes a
lock or ledger. It reports `owned` only when the recorded UUID, token,
configuration digest, and graph ID all match OVS. Destroy preflights the entire recorded set,
then conditions each deletion in the same OVSDB transaction on the UUID and
all three ownership markers. A missing owned bridge is tolerated; a replacement is never
deleted. Recover is reserved for an interrupted `creating` state and discovers
an atomic bridge mutation by its token and digest even if interruption happened
before its UUID was saved.

`GRAPHX_TEST_FAIL_AFTER_MUTATION` and `GRAPHX_TEST_CRASH_AFTER_MUTATION` are bounded verifier-only
failure points indexed by bridge mutation. The former exercises in-process
rollback; the latter exits immediately so the recovery command can prove
durable crash cleanup. Do not set them during normal operation.

## Failure handling

Do not delete a collision manually until its identity is understood. Preserve
the state file and inspect both it and OVS:

```text
graphx infra status CONFIG
ovs-vsctl get Bridge BRIDGE _uuid external_ids
```

Use `recover` only for an interrupted state. A ready run must use `destroy`.
Configuration edits do not grant ownership: cleanup is controlled by the
recorded random token, digest, graph ID, and UUID. State roots, locks, and state
files must not be symlinks, and permissive or malformed state is rejected.
