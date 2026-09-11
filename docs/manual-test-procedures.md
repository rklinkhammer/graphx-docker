# Manual test procedures for macOS and Linux

This runbook owns platform-specific interactive evidence and cleanup. It turns
the automated GraphX profiles into repeatable operator acceptance procedures
without redefining their test inventory. It complements the profile summary in
[`test-procedure.md`](test-procedure.md) and the exhaustive expected-result
reference in [`test-reference.md`](test-reference.md). Use the consolidated
[`demo guide`](demo-guide.md) when the objective is exploration rather than an
acceptance decision.

## 1. Evidence rules

Record each result as one of these categories:

- **runtime verified:** the described behavior ran on the stated host;
- **automated simulation:** a bounded substitute or mock passed;
- **inspection only:** configuration, plan, or source was checked without the
  behavior running;
- **not applicable:** the platform cannot provide the required behavior;
- **blocked:** the host lacks a prerequisite or an external dependency failed;
- **failed:** a required check ran and did not meet its expected result.

OrbStack, the Lima guest, and a dry-run are separate evidence boundaries. None
is native-Linux evidence for a native host kernel, KVM, or host packet capture.
Lima does provide valid ARM64 Linux system-OVS, namespace, nftables, netem,
veth/TAP, and TCG evidence when those behaviors actually run in the guest. Do
not convert an unavailable or skipped gate into a pass or relabel Lima evidence
as native Linux.

## 2. Common preparation

Run from the repository root as the normal login user:

```sh
git rev-parse HEAD
git status --short
uname -a
cmake --version
ninja --version
docker version
docker compose version
docker context show
docker info
node --version
npm --version
python3 --version
```

Record the commit, worktree state, OS/architecture, and tool versions. Use a
clean candidate unless the report explicitly identifies every local change.
Unset deployment values inherited from another example:

```sh
unset GRAPHX_CONFIG GRAPHX_OVERRIDES GRAPHX_CONTROL_TOKEN
unset GRAPHX_OBSERVATION_TOKEN GRAPHX_TELEMETRY_SHARED_SECRET
unset GRAPHX_PREVIOUS_CREDENTIALS_FILE GRAPHX_RUNTIME_IDENTITIES_FILE
```

Check common ports before a full or interactive run. Choose unused loopback
ports through the documented overrides rather than stopping unrelated services.

```sh
lsof -nP -iTCP:8080 -sTCP:LISTEN || true
lsof -nP -iTCP:28080 -sTCP:LISTEN || true
docker ps --format '{{.Names}} {{.Ports}}'
```

The verification wrapper stops at the first failed gate and writes one combined
log below `outputs/verification/`. Preserve that path in the test report.

## 3. macOS manual acceptance

### 3.1 Prerequisites and platform boundary

Install CMake 3.25 or newer, Ninja, OpenSSL 3, Node.js/npm, Python 3, curl,
OrbStack, cppcheck, Homebrew LLVM 21, and Lima. QEMU tooling is installed in the
Lima guest for the canonical TAP/OVS demo. Wireshark/TShark on macOS is optional
unless host-side capture decoding is part of the acceptance scope.

```sh
brew install cmake ninja openssl@3 node python llvm@21 cppcheck
brew install orbstack lima
```

Open OrbStack, select its Docker context, and wait for the engine and Compose to
be ready before starting the lengthy `full` profile:

```sh
open -a OrbStack
orb status
docker context use orbstack
docker info
docker compose version
```

The verification wrapper discovers the keg-only LLVM 21 tools and selects a
host-compatible installed macOS SDK. On macOS 26,
Homebrew LLVM 21 AddressSanitizer has an upstream startup hang, so GraphX runs
LLVM 21 UBSan locally and relies on Linux and macOS 15 CI for required ASan
coverage. The log must state this substitution.

OrbStack supplies ordinary Docker/Compose application acceptance only. It is
not the GraphX privileged network backend. System OVS, semantic MACVLAN/IPVLAN
profiles, Linux namespaces, veth/TAP, nftables, `tc netem`, capture, and the
static-route laboratory run in the dedicated Lima guest. KVM is unavailable for
the checked-in x86_64 guest on Apple Silicon; record its Lima execution as TCG.

### 3.2 Build and automated acceptance

Run the fast development gate first:

```sh
scripts/verify.sh quick
```

Expected: configuration and build complete, then every configured CTest passes.
Before accepting a candidate, run the complete macOS profile on an unused
Docker telemetry port:

```sh
GRAPHX_DOCKER_TEST_HTTP_PORT=28180 scripts/verify.sh full
```

Expected gates are formatting, clang-tidy/cppcheck, platform-safe sanitizers,
two bounded fuzz targets, C++23 and C++20 CTest, finite process pipelines,
telemetry and web tests/build, configuration and projection checks, the portable
examples, and Docker acceptance. The last line must be `PASS: full verification
completed` and include the retained log path.

### 3.3 Required interactive checks

Run the standard system demo:

```sh
scripts/demo.sh start
scripts/demo.sh verify
scripts/demo.sh status
scripts/demo.sh token
```

Open the console URL printed by `start`, normally <http://127.0.0.1:8080>.
Confirm Application and Network views render,
both TCP edges connect, counters advance without refresh, sink values are twice
their sequence, Pause/Resume works after entering the token, History remains
usable after view changes, and a capture can be downloaded when capture is
enabled. Then stop it:

```sh
scripts/demo.sh stop
```

When the browser reaches a remote Linux demo through a local port forward, the
WebSocket allowlist must use the browser-visible origin. For example, for local
port 18080 forwarded to Linux port 8080, start the remote demo with:

```sh
GRAPHX_ALLOWED_ORIGINS="http://localhost:18080,http://127.0.0.1:18080" \
  scripts/demo.sh start
```

Then forward `18080` to remote `127.0.0.1:8080`, open
<http://localhost:18080>, and require the console to show **LIVE** with counters
advancing without refresh. The complete distinction between Docker port
publishing and an external tunnel is documented in
[`complete-system-demo.md`](complete-system-demo.md#forwarded-browser-port-versus-docker-published-port).

Exercise the privileged network lifecycle in the dedicated Lima guest. Start
and verify the environment from macOS:

```sh
infrastructure/lima/start.sh
infrastructure/lima/verify.sh
limactl shell graphx
```

After those checks, return to the macOS shell. The cross-platform dispatcher
uses the verified VM-native binary and invokes the same canonical system-OVS
launcher inside Lima:

```sh
scripts/network-lab.sh mixed-network plan
scripts/network-lab.sh mixed-network up
scripts/network-lab.sh mixed-network status
scripts/network-lab.sh mixed-network down
```

For the complete Linux/Lima network regression, keep build and runtime artifacts on
the guest-native filesystem:

```sh
GRAPHX_BUILD_DIR=/var/lib/graphx/manual/build \
GRAPHX_CXX20_BUILD_DIR=/var/lib/graphx/manual/build-cxx20 \
GRAPHX_ALLOW_PRIVILEGED_TESTS=1 scripts/test-features.sh linux-network
```

Capture and netem faults are declarative version-2 lifecycle objects. Do not
restore the removed imperative `fault.sh` or userspace-OVS simulator.

Run the portable external-device profile:

```sh
examples/sdr-node/simulated/scripts/demo.sh start
examples/sdr-node/simulated/scripts/demo.sh verify
examples/sdr-node/simulated/scripts/demo.sh status
examples/sdr-node/simulated/scripts/demo.sh stop
```

Confirm raw UDP sample, mTLS control, and raw result edges update in the GUI;
packet history and Ethernet capture must remain distinct from GraphX message
history. Inspect the route-policy model without claiming native routing evidence:

```sh
examples/static-route-policy/scripts/inspect.sh
```

When QEMU is in scope, build the shared guest and run the canonical TAP/OVS
profile inside Lima:

```sh
examples/qemu-node/scripts/build.sh
examples/qemu-node/scripts/demo.sh start
examples/qemu-node/scripts/demo.sh verify
examples/qemu-node/scripts/demo.sh status
examples/qemu-node/scripts/demo.sh stop
```

Apple Silicon Lima selects TCG for the x86_64 guest. Record requested, selected,
and QMP-proven accelerator values separately. The host external/slirp profile
is retained only as an explicitly deprecated compatibility path.

### 3.4 macOS cleanup audit

```sh
docker ps --filter name=graphx --format '{{.Names}} {{.Status}}'
docker network ls --format '{{.Name}}' | grep -E '^(graphx|gx-)' || true
ps -axo pid,command | grep -E '[q]emu-system|[p]acket_observer|[s]dr-node' || true
```

These commands audit OrbStack resources. Audit privileged state separately
inside Lima with the Linux cleanup commands in section 4.5. Expected: no active
resources from stopped demos. Retained captures, history databases,
credentials, build trees, verification logs, and zero-byte per-graph lock files
are expected artifacts, not active resources.

## 4. Linux manual acceptance

### 4.1 Prerequisites and host safety

Install the common toolchain plus Docker Engine/Compose, LLVM/Clang 21,
clang-format 21, clang-tidy 21, cppcheck, Open vSwitch, iproute2, nftables,
dumpcap, tshark, tcpdump, curl, Node.js/npm, Python 3, and OpenSSL 3 development
files. QEMU and accessible `/dev/kvm` are needed only for QEMU KVM acceptance.

Use a dedicated native Linux test host. Review the fixed names and private
subnets in each example README. Do not attach a physical interface, run the
entire suite as root, or delete a colliding resource whose ownership is unknown.
The scripts request `sudo` only around host-network operations.

### 4.2 Portable, quality, and Docker baseline

```sh
scripts/verify.sh quick
GRAPHX_DOCKER_TEST_HTTP_PORT=28180 scripts/verify.sh full
```

Linux acceptance requires LLVM 21 ASan and UBSan; an ASan skip is not an
equivalent pass. Require the same final full-profile marker and retained log as
on macOS.

During portable acceptance, all checked-in topologies are validated and
inspected. Infrastructure plans are generated only for version-2 examples;
version-1 transport examples are checked for the required pre-mutation refusal.

### 4.3 Privileged native-network profile

After reviewing the generated plans and confirming that the example subnets and
fixed resource names are free, opt in explicitly:

```sh
./build/dev/graphx infra create examples/macvlan/graphx.yaml --dry-run
./build/dev/graphx infra create examples/ipvlan-l2/graphx.yaml --dry-run
./build/dev/graphx infra create examples/ipvlan-l3/graphx.yaml --dry-run
./build/dev/graphx infra create examples/mixed-network/graphx.yaml --dry-run
GRAPHX_ALLOW_PRIVILEGED_TESTS=1 scripts/verify.sh native-linux
```

The profile reruns portable acceptance, then proves native UDP broadcast/live
capture, OVS-backed MACVLAN/IPVLAN semantic profiles, namespaces, nftables,
netem, the external SDR OVS/SPAN profile, and two static-route/policy cycles. A pass requires
`dumpcap` and TShark; the profile refuses to start without them.

### 4.4 Manual native observations

Use the individual launchers when reviewing network behavior interactively:

```sh
examples/macvlan/scripts/up.sh
examples/macvlan/scripts/status.sh
examples/macvlan/scripts/down.sh

examples/ipvlan-l2/scripts/up.sh
examples/ipvlan-l2/scripts/status.sh
examples/ipvlan-l2/scripts/down.sh

examples/ipvlan-l3/scripts/up.sh
examples/ipvlan-l3/scripts/status.sh
examples/ipvlan-l3/scripts/down.sh

examples/mixed-network/scripts/up.sh
examples/mixed-network/scripts/status.sh
examples/mixed-network/scripts/down.sh
```

Require real sink delivery, declared IP/MAC assignments, expected OVS and router
state, readable Ethernet captures where requested, and no residual host
resources. For native UDP broadcast with packet proof:

```sh
GRAPHX_BUILD_DIR="$PWD/build/dev" GRAPHX_VERIFY_LIVE_CAPTURE=1 \
  examples/udp-broadcast/run-native-linux.sh
examples/udp-broadcast/down-native-linux.sh
examples/udp-broadcast/down-native-linux.sh
```

For QEMU, run both TCG and KVM and follow the accelerator-denial and capture
inspection procedure in [`qemu-demos.md`](qemu-demos.md#manual-linux-acceptance-procedure).
For external SDR, require OVS/SPAN state, packet delivery, mTLS control, GUI,
capture/history, and marker-checked cleanup as documented in the
[`external SDR profile`](../examples/sdr-node/external/README.md).

The static-route/policy lab requires two complete cycles:

```sh
examples/static-route-policy/scripts/demo.sh up
examples/static-route-policy/scripts/demo.sh status
sudo ip netns exec gx-route-left-end ping -c 1 -W 1 10.64.2.10
! sudo ip netns exec gx-route-middle-end ping -c 1 -W 1 10.64.1.10
! sudo ip netns exec gx-route-left-end ping -c 1 -W 1 10.64.30.10
sudo ip netns exec gx-route-router nft list chain inet graphx forward
examples/static-route-policy/scripts/demo.sh apply-route
sudo ip netns exec gx-route-router ip route show 10.64.30.10/32
sudo ip netns exec gx-route-left-end ping -c 1 -W 1 10.64.30.10
examples/static-route-policy/scripts/demo.sh clear-route
examples/static-route-policy/scripts/demo.sh down
```

Repeat the complete sequence once. For each cycle, prove allowed delivery, the
expected denied and missing-route failures, an advancing named nftables counter,
the exact kernel-route transition, and preservation of unrelated canary
resources. The historical GUI, receiver, capture, and failure-injection record
from the retired launcher is archived at
[`archive/legacy-phases/phase_14_verification.md`](archive/legacy-phases/phase_14_verification.md).

### 4.5 Linux cleanup audit

Run each matching teardown twice when its README marks teardown idempotent, then
inspect the host:

```sh
docker ps --filter name=graphx --format '{{.Names}} {{.Status}}'
docker network ls --format '{{.Name}}' | grep -E '^(graphx|gx-)' || true
sudo ip netns list
sudo ovs-vsctl show
ip -brief link
ps -ef | grep -E '[q]emu-system|[d]umpcap|[t]cpdump|[p]acket_observer' || true
```

Compare these results with the recorded baseline. Remove only resources proven
to belong to the completed run. The relevant launcher state and intrinsic
ownership markers take precedence over a matching name.

## 5. Acceptance report template

Record at least:

```text
Commit and worktree:
Host OS, version, architecture:
Docker/Compose, compiler, CMake, Node, Python versions:
Profile command and log:
Automated result:
Interactive demos run:
Runtime-verified evidence:
Inspection-only or simulated evidence:
Unavailable/blocked gates and reason:
Cleanup result:
Residual risks or failures:
Overall platform verdict: PASS | FAIL | INCOMPLETE
```

A macOS host pass establishes portable and OrbStack Compose behavior only.
Privileged Lima results are recorded separately as Linux ARM64 guest evidence.
A complete native-Linux platform verdict consists of a successful `full`
profile plus the explicitly authorized `native-linux` profile and any
feature-specific manual evidence required by the change.
