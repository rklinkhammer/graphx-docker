# GraphX 1.1.0 architecture

GraphX separates declarative intent, application execution, Linux data-plane
resources, and observation.

1. `graphx.yaml` defines graph nodes and edges, transports, deployment, OVS
   networking, and observability.
2. The C++ loader validates that model and emits deterministic normalized JSON.
3. Transport implementations move GraphX envelopes over TCP, UDP, Unix-domain
   sockets, shared memory, or in-process queues.
4. The infrastructure lifecycle realizes system OVS bridges, veth/TAP attachments,
   namespaces, routes, policy, capture, and bounded network faults.
5. The telemetry service consumes normalized configuration and runtime events to
   provide health, topology, metrics, trace, history, capture, and authorized
   control APIs.

Each edge stores exactly one transport-specific configuration. TCP owns retry and
TLS policy, UDP owns datagram and multicast policy, and the queue-based transports
own capacity and backpressure policy. External data-plane edges use an explicit
observed-only wrapper around TCP or UDP configuration, so they cannot be passed to
the managed transport factory accidentally. This typed model prevents unrelated
settings, such as TLS on UDP or queue capacity on TCP, from existing after parsing.

Docker Compose owns application processes and their management network. It does not
own the GraphX data plane. Native Linux hosts or the GraphX Lima guest own privileged
OVS resources; OrbStack runs portable Compose workloads on macOS.

## Trust and ownership boundaries

The [instance identity contract](instance-identity.md) specifies the design for
deployment and execution identity, including authorization and recovery boundaries.
It explicitly identifies the current implementation limits; it does not imply
that examples can already run concurrently without collisions.

Configuration and state files are bounded, regular, non-symlink inputs. Network
resources carry a graph ID, configuration hash, owner token, and stable platform
identity. Destruction verifies those identities and refuses ambiguous cleanup.
Credentials are projected as files, excluded from normalized configuration and
telemetry, and redacted from logs and APIs.

## Runtime boundaries

- Graph-managed execution is acyclic; external raw-device relationships may form
  control cycles.
- QEMU networking uses a TAP attached to OVS. QMP handles VM state evidence and
  bounded pause/resume actions.
- Semantic profiles (`ethernet`, `macvlan`, and IPVLAN variants) are implemented by
  OVS configuration and flow rules.
- Application capture and OVS SPAN capture are separate evidence sources.
- Fault injection is bounded and owned by the infrastructure ledger.

The [`complete user guide`](user-guide.md) is the primary operational entry point.
Detailed current contracts are in [`configuration.md`](configuration.md),
[`network-infrastructure.md`](network-infrastructure.md),
[`runtime-lifecycle.md`](runtime-lifecycle.md), and
[`observability.md`](observability.md).
