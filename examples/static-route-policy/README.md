# Static route and policy laboratory

This Phase 14 example is a native-Linux acceptance laboratory for three outcomes
that a topology diagram alone cannot prove:

- `allowed-flow` crosses the left and middle Layer-2 domains and is received;
- `denied-flow` reaches the router but is dropped by the named nftables policy;
- `routed-flow` initially fails because `10.64.30.10/32` is absent from the
  router, then succeeds only after the declared manual route is applied.

The GraphX model is also portable. On macOS and other non-Linux hosts, use
`scripts/inspect.sh` to validate the configuration and inspect all create,
status, route, and destroy commands without changing the host.

## Topology

```text
left 10.64.1.10 -- br-route-left --+
                                      gx-route-router -- br-route-right -- right 10.64.3.10
middle 10.64.2.10 -- br-route-middle +                                  + 10.64.30.10/32
```

Each domain is a separate Open vSwitch bridge. The central router is one Linux
network namespace with three interfaces. Every bridge has an all-traffic mirror
to a dedicated capture interface. The endpoint namespaces and the router are
lab-owned, fixed-name resources; startup refuses collisions and cleanup checks
ownership markers before removal.

`network.routers[].routes[].install: manual` declares a route without installing
it during `graphx infra create`. `graphx infra route apply` and `clear` operate
only on an exact route already declared in the validated configuration.

## Portable inspection

Build the development preset, then run:

```sh
examples/static-route-policy/scripts/inspect.sh
```

This is configuration/code inspection, not packet or kernel verification.

## Native Linux run

Prerequisites are Docker Compose, Open vSwitch, iproute2, nftables, dumpcap,
curl, Python 3, OpenSSL, sudo access, and a built GraphX CLI. The fixed
`10.64.1.0/24`, `10.64.2.0/24`, `10.64.3.0/24`, and `10.64.30.10/32` ranges must
be unused. Override the CLI with `GRAPHX_BIN=/absolute/path/to/graphx` and the
loopback GUI port with `GRAPHX_ROUTE_GUI_PORT` if necessary.

```sh
examples/static-route-policy/scripts/demo.sh start
examples/static-route-policy/scripts/demo.sh status
examples/static-route-policy/scripts/demo.sh verify
examples/static-route-policy/scripts/demo.sh apply-route
examples/static-route-policy/scripts/demo.sh clear-route
examples/static-route-policy/scripts/demo.sh stop
examples/static-route-policy/scripts/demo.sh stop
```

Open `http://127.0.0.1:8080/`. The application and network views distinguish
green `allowed`, red `policy-denied`, amber `missing-route`, and blue dashed
`route-applied` edges. Select an edge to see its evidence type. The route action
The missing-route state requires a successful send, receiver absence, and an
absent exact kernel route. Route presence requires the declared destination,
next hop, and device to match.
is intentionally a CLI operation, not a general-purpose privileged web control.

The run directory printed by the script contains:

- `route-policy.pcapng`, bounded to 64 MiB and made readable by the invoking user;
- separate timestamped sender and receiver logs for every bounded one-way UDP probe;
- `dumpcap.log`;
- `diagnostic-state.json`, the small, strict evidence projection consumed by the GUI.

The receiver result proves positive delivery. The denied flow requires both a
receiver timeout and an increased counter on the named nftables rule. The
missing-route state requires receiver absence and an absent exact kernel route.
PCAP is corroborating evidence and can be filtered with:

```sh
tshark -r RUN_DIRECTORY/route-policy.pcapng \
  -Y 'udp.port == 18601 || udp.port == 18602 || udp.port == 18603'
```

## Cleanup and failure recovery

`start` rolls back resources it created when setup fails or is interrupted.
`stop` is repeatable and retains evidence. It refuses to delete fixed-name
resources whose ownership markers do not match the saved run token. If the
state file is lost, inspect the `graphx_route_owner` OVS external IDs and
`graphx-route:<token>` interface aliases before performing any manual cleanup;
never assume a same-named resource is disposable.

Native acceptance requires two complete start/verify/apply/clear/stop cycles,
checks of kernel routes, OVS mirrors, nftables counters, capture readability,
GUI state transitions, and proof that an unrelated namespace/bridge/link remains
untouched. See `prompt/verifier.md` for the independent Phase 14 procedure.
