# GraphX independent verification work package

Independently verify **Migration M1: Lima macOS execution foundation** in
`~/workspace/graphx-docker` against ADR 0016, ADR 0017,
`migration_m0_baseline.md`, and acceptance identifiers LIMA-001 through
LIMA-010. Use `prompt/ovs_migration_implementation_plan.md` only for scope and
sequencing. Read `migration_m1_handoff.md` only as an implementation claim.
Write the result to `migration_m1_verification.md`.

## Independence and evidence

- Derive the traceability matrix from the decisions and implementation rather
  than copying the handoff.
- Do not make product fixes, discard work, commit, publish, alter unrelated
  macOS networking, or trust filenames, dry-runs, static tests, or claimed logs
  as runtime proof.
- Classify evidence as macOS host runtime, Lima guest runtime, portable runtime,
  automated simulated dependency, inspection only, blocked, not applicable, or
  failed.
- Only a real Lima guest can prove M1 OVS, namespace, veth, TAP, nftables,
  netem, capture, service, storage, and cleanup claims.
- Keep all commands, waits, packets, files, retries, and cleanup bounded.

## Verification procedure

### 1. Baseline and definition

Record repository status/history, macOS version and architecture, Lima version,
virtualization support, available disk/memory, and existing Lima instances.
Review both M0 ADRs, the VM definition, every lifecycle script, tests, and
operator documentation. Confirm that M1 does not change configuration semantics
or the current GraphX network planner.

Run the macOS quick profile and projection check. Compare affected results with
`migration_m0_baseline.md`; explain any difference rather than modifying the
baseline.

### 2. Clean creation and provisioning

From an absent GraphX instance, run the documented start/provision workflow.
Inspect the actual VM configuration, architecture, mounts, port forwards,
service listeners, users/groups, package versions, and storage filesystems.

Confirm Docker is rootful, Docker and OVS are init-managed and healthy, and no
privileged socket or service is exposed beyond the documented boundary. Run
provisioning again and prove idempotence.

### 3. Runtime primitives

Run `infrastructure/lima/verify.sh`, then independently inspect:

- OVS bridge and port UUIDs and `datapath_type=system`;
- namespace and network-namespace identity;
- veth peer identities and placement;
- TAP type, owner, permissions, and OVS attachment;
- addresses, routes, nftables objects, and qdiscs;
- the bounded packet exchange and packet capture;
- Docker container identity and cleanup;
- VM-local locations for Docker, OVS, QEMU, capture, and GraphX state.

Repeat the complete verification cycle twice. Do not infer packet delivery from
configuration alone.

### 4. Adversarial lifecycle

Test occupied `gx-m1-*` names, forged/stale state, missing required tools,
stopped Docker and OVS services, an unavailable capture permission, source-mount
failure, native-state path on the wrong filesystem, interruption after each
mutation stage, partial cleanup, repeated cleanup, and immediate retry.

Every failure must be bounded and actionable. It must remove only resources
created by the active attempt and preserve same-named replacements or unrelated
objects whose recorded identity no longer matches.

### 5. Isolation and teardown

Compare macOS and guest state before and after both cycles. Confirm all owned
containers, namespaces, links, TAPs, OVS bridges/ports, nftables objects,
qdiscs, processes, listeners, and temporary files are gone. Confirm retained
evidence is bounded and readable.

Stop and restart the VM. Verify source persistence and intended VM-local state
behavior. Test the documented deliberate instance-removal procedure only after
resolving the exact instance and confirming that no unrelated instance or host
path is targeted.

### 6. Architecture honesty and documentation

Confirm documentation distinguishes:

- the Lima environment delivered by M1;
- the still-current version-1 Docker-driver network implementation;
- the later version-2 OVS-only application data plane;
- current QEMU slirp from future TAP attachment;
- TCG-capable Apple Silicon evidence from native-Linux KVM evidence.

Verify that operator commands, paths, prerequisites, storage boundaries,
security risks, troubleshooting, and cleanup steps match runtime behavior.

## Acceptance matrix and verdict

Report LIMA-001 through LIMA-010 with requirement, implementation path,
independent evidence, status (`Implemented`, `Partial`, `Missing`, or `Not Yet
Applicable`), and precise remediation. Also report commands/results,
architectural drift, unsafe defaults, stale documentation, resource leaks, and
environmental limitations.

M1 passes only when a real macOS/Lima environment completes two independent
provision/verify/cleanup cycles, real system-OVS/veth/TAP packet evidence is
observed, high-I/O state is proven VM-local, and host/guest state returns to its
documented baseline. Portable or inspection-only evidence makes the overall
verdict incomplete.
