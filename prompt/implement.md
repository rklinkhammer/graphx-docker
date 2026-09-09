# GraphX implementation work package

Implement **Migration M1: Lima macOS execution foundation** in
`~/workspace/graphx-docker`. M1 follows the decisions in ADR 0016 and ADR 0017
and the frozen evidence in `migration_m0_baseline.md`. The complete sequence is
recorded in `prompt/ovs_migration_implementation_plan.md`.

## Objective

Deliver a reproducible, disposable Lima Linux VM that becomes the supported
macOS host for later GraphX OVS networking work. The VM must run rootful Docker,
the OVS system datapath, Linux namespace/veth/TAP/routing/nftables/netem tools,
QEMU, packet-capture tools, and the current GraphX portable build and tests.

M1 establishes the execution environment only. Do not implement configuration
version 2, reinterpret version-1 network drivers, attach application containers
to OVS, migrate examples, or replace QEMU slirp in this phase.

## Working rules

- Read both M0 ADRs, the ADR index, baseline record, current architecture,
  support/security documentation, verification scripts, network examples, and
  this contract before editing.
- Preserve unrelated work. Do not commit, push, publish, deploy, or mutate the
  macOS host network beyond Lima's documented VM and loopback forwarding.
- Keep all waits, downloads, retries, logs, disk allocations, verification
  resources, and cleanup operations bounded.
- Pin or otherwise deterministically constrain the guest distribution and
  provisioned package set. Record versions needed to reproduce the result.
- Do not call a Docker Desktop simulation or a macOS portable test native OVS
  evidence.
- Never place Docker data, OVS databases, QEMU disks, active PCAPs, or GraphX
  run ownership state on the macOS shared source mount.
- Do not expose the Docker socket, OVS control socket, QMP socket, or privileged
  services beyond the minimum documented local boundary.

## Required implementation

Create `infrastructure/lima` containing:

- `graphx.yaml`: authoritative Lima instance configuration;
- `provision.sh`: idempotent bounded guest provisioning;
- `start.sh`: start or create the expected named instance without adopting a
  differently configured instance;
- `verify.sh`: non-destructive environment checks plus transactionally cleaned
  disposable Linux network checks;
- `stop.sh`: stop the instance without deleting source or retained evidence;
- `README.md`: prerequisites, lifecycle, storage, security, verification,
  troubleshooting, reset/removal, and limitations.

Use a fixed GraphX Lima instance name and an ARM64 Linux guest on Apple Silicon.
Mount the repository at `/workspace/graphx-docker`. Allocate a VM-native GraphX
state root and verify that it is not on the shared mount. Provision at least:

- rootful Docker Engine and Compose;
- Open vSwitch with a working system datapath;
- iproute2, nftables, `tc`, `ip netns`, and TUN/TAP support;
- QEMU system tools required by the existing examples;
- tcpdump, dumpcap/TShark, curl, OpenSSL, Python, CMake, Ninja, a supported C++
  compiler, and repository build dependencies.

Prefer system services managed by the guest init system. Make repeated
provisioning safe. Fail with actionable diagnostics when virtualization, mount,
disk, package, service, or kernel facilities are unavailable.

The verification lifecycle must prove, with fixed disposable `gx-m1-*` names:

1. the expected VM identity, architecture, source mount, and native-state mount;
2. rootful Docker and Compose operation with a bounded disposable container;
3. OVS database/vswitchd health and a disposable system-datapath bridge;
4. a disposable namespace connected to OVS through veth;
5. a disposable TAP attached to OVS;
6. address assignment and a bounded packet exchange through the disposable
   topology;
7. nftables and netem availability in a disposable namespace;
8. packet capture on a disposable observation interface when permissions allow;
9. the GraphX 1.1.0 portable baseline and projection check from the mounted
   checkout; and
10. exact cleanup of every `gx-m1-*` container, namespace, link, TAP, bridge,
    port, rule, qdisc, process, and temporary file.

Record before/after snapshots sufficient to show that unrelated guest Docker,
OVS, namespace, link, route, rule, nftables, process, and listener state was not
changed. Cleanup must verify resource identity before deletion and remain safe
after partial setup or interruption.

Do not require KVM on Apple Silicon. Report the QEMU accelerator capability
honestly; current x86_64 and future MPC8360E/PowerPC guests are expected to use
emulation in this environment unless runtime evidence proves otherwise.

## Tests and documentation

- Add portable static tests for the Lima YAML, scripts, bounds, fixed names,
  storage rules, service configuration, and destructive-command safety.
- Validate shell syntax and formatting.
- Run `scripts/verify.sh quick` and `graphx project --check` on macOS.
- Inside Lima, run the current quick verification profile and all M1 runtime
  checks twice from a clean state.
- Test interrupted verification, repeated cleanup, occupied disposable names,
  an unavailable required service, and immediate retry.
- Update README/support/security/architecture navigation only where necessary
  to describe the new optional M1 environment. Do not claim the later OVS-only
  application data plane is implemented.

## Acceptance identifiers

- **LIMA-001 Definition:** the VM configuration is deterministic, bounded, and
  exposes only the intended source mount and loopback services.
- **LIMA-002 Provisioning:** repeated provisioning produces the required tools
  and healthy rootful Docker/OVS services.
- **LIMA-003 Storage:** high-I/O and privileged runtime state is VM-local.
- **LIMA-004 Linux primitives:** system OVS, namespace, veth, TAP, routing,
  nftables, netem, and capture checks pass with real runtime evidence.
- **LIMA-005 GraphX baseline:** the mounted checkout builds and passes the M0
  portable baseline without projection drift.
- **LIMA-006 Lifecycle:** start, verify, stop, interruption, retry, and repeated
  cleanup are bounded and deterministic.
- **LIMA-007 Isolation:** unrelated macOS and guest state remains unchanged.
- **LIMA-008 Security:** privileged sockets and services are not broadly
  exposed; scripts reject unsafe state and targets.
- **LIMA-009 Architecture honesty:** no v2, container-veth application path,
  QEMU TAP profile, or KVM capability is claimed prematurely.
- **LIMA-010 Documentation:** an operator can reproduce, inspect, troubleshoot,
  stop, and deliberately remove the environment.

## Required evidence and exit

Write `migration_m1_handoff.md` with the requirement matrix, changed paths,
exact commands/results, macOS and guest versions, repository state, artifact
locations, before/after comparisons, limitations, and independent-verifier
instructions.

M1 is complete only after two real Lima create/provision/verify/cleanup cycles
pass on macOS and the VM-local system OVS/veth/TAP checks are recorded. Static
inspection, YAML validation, Docker Desktop, or a native-Linux result from a
different host cannot substitute for the Lima runtime gate.
