# M4 managed container veth ownership

M4 separates Compose management connectivity from the GraphX data plane. Docker
Compose continues to start and stop application processes. GraphX creates each
declared `container_veth` pair, attaches its host end to an owned system-datapath
OVS bridge, and moves its peer into the verified service container.

## Configuration contract

A version-2 deployment with container attachments declares `deployment.project`.
Each attachment declares a service owner, network, static CIDR address, target
interface, host peer, OVS switch, and optionally a MAC, MTU, and static routes.
Host interface names must be unique. Migration supplies deterministic interface
and switch names but does not invent absent legacy addresses; realization refuses
an attachment until its address is reviewed and declared.

## Identity contract

GraphX resolves a running container with both
`com.docker.compose.project=<project>` and
`com.docker.compose.service=<service>`. Exactly one match is allowed. Before any
host mutation, GraphX verifies the configured image and labels and records the
full 64-character container ID plus the inode of `/proc/<pid>/ns/net`.

The ownership ledger also records the attachment ID, host and target interface
names, host and peer ifindices, OVS Port and Interface UUIDs, and the intrinsic
GraphX owner/configuration/graph markers. Interface aliases carry the owner token
and attachment ID on both veth ends. Names alone never establish ownership.

## Lifecycle behavior

`infra create` resolves and preflights every endpoint before publishing state.
It creates bridges, then veth endpoints, applies the declared MAC, address, MTU,
and routes inside the recorded namespace, and brings both ends up. An interrupted
create rolls endpoints back before bridges. `infra destroy` preflights the full
resource set and rechecks identity during deletion.

`infra status` re-resolves the Compose service and compares its full ID and
namespace inode with the ledger. A restart or replacement is unhealthy and is
never silently adopted. The explicit safe reattachment path is `infra destroy`
followed by `infra create`; deleting the host veth does not require entering the
replacement namespace.

Linux namespace attachments, QEMU TAP, semantic-profile flows, mirrors, general
router policy, faults, and capture realization remain later milestones.
