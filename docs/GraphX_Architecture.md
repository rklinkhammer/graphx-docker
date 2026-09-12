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

Docker Compose owns application processes and their management network. It does not
own the GraphX data plane. Native Linux hosts or the GraphX Lima guest own privileged
OVS resources; OrbStack runs portable Compose workloads on macOS.

## Trust and ownership boundaries

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

Detailed current contracts are in [`configuration.md`](configuration.md),
[`network-infrastructure.md`](network-infrastructure.md),
[`runtime-lifecycle.md`](runtime-lifecycle.md), and
[`observability.md`](observability.md).
