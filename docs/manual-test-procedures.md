# Manual test procedures for macOS and Linux

This runbook turns the automated GraphX profiles into repeatable operator
acceptance procedures. It complements the profile summary in
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

Docker Desktop, a Linux VM, and a dry-run are not native-Linux evidence for
macvlan, IPvlan, OVS system datapaths, namespaces, nftables, `tc netem`, KVM, or
host packet capture. Do not convert an unavailable or skipped gate into a pass.

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
Docker Desktop with Compose, cppcheck, and Homebrew LLVM 21. QEMU is needed only
for the external-QEMU demo. Wireshark/TShark is optional unless capture decoding
is part of the acceptance scope.

```sh
brew install cmake ninja openssl@3 node python llvm@21 cppcheck
brew install --cask docker
```

Start Docker Desktop and wait for `docker info` to succeed. The verification
wrapper discovers the keg-only LLVM 21 tools and active macOS SDK. On macOS 26,
Homebrew LLVM 21 AddressSanitizer has an upstream startup hang, so GraphX runs
LLVM 21 UBSan locally and relies on Linux and macOS 15 CI for required ASan
coverage. The log must state this substitution.

Native macvlan/IPvlan, Linux namespaces, host OVS, nftables, `tc netem`, KVM,
and the static-route native laboratory are not applicable on macOS. The
mixed-network macOS profile is a Docker Desktop userspace-OVS simulation, not
equivalent evidence.

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

Exercise the macOS network substitute and inspect the OVS/router container:

```sh
examples/mixed-network/scripts/macos-up.sh
examples/mixed-network/scripts/status.sh
docker logs gx-ovs-ovs-router-1
examples/mixed-network/scripts/fault.sh apply
examples/mixed-network/scripts/fault.sh clear
examples/mixed-network/scripts/macos-down.sh
```

Run the portable external-device profile:

```sh
examples/sdr-node/simulated/scripts/demo.sh start
examples/sdr-node/simulated/scripts/demo.sh verify
examples/sdr-node/simulated/scripts/demo.sh status
examples/sdr-node/simulated/scripts/demo.sh stop
```

Confirm raw UDP sample, mTLS control, and raw result edges update in the GUI;
packet history and Ethernet capture must remain distinct from GraphX message
history. Inspect the Phase 14 model without claiming native routing evidence:

```sh
examples/static-route-policy/scripts/inspect.sh
```

When QEMU is in scope, build the shared guest and run the host-QEMU profile:

```sh
examples/qemu-node/scripts/build.sh
examples/qemu-node/external/scripts/demo.sh start --accel auto
examples/qemu-node/external/scripts/demo.sh verify
examples/qemu-node/external/scripts/demo.sh status
examples/qemu-node/external/scripts/demo.sh stop
```

Apple Silicon selects TCG for the x86_64 guest. HVF is valid only on Intel
macOS. Record requested, selected, and QMP-proven accelerator values separately.

### 3.4 macOS cleanup audit

```sh
docker ps --filter name=graphx --format '{{.Names}} {{.Status}}'
docker network ls --format '{{.Name}}' | grep -E '^(graphx|gx-)' || true
ps -axo pid,command | grep -E '[q]emu-system|[p]acket_observer|[s]dr-node' || true
```

Expected: no resources from stopped demos. Retained captures, history databases,
credentials, build trees, and verification logs are expected artifacts, not
active resources.

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
capture, macvlan, IPvlan L2/L3, OVS, namespaces, nftables, netem, the external
SDR OVS/SPAN profile, and two static-route/policy cycles. A pass requires
`dumpcap` and TShark; the profile refuses to start without them.

### 4.4 Manual native observations

Use the individual launchers when reviewing network behavior interactively:

```sh
examples/macvlan/scripts/up.sh
examples/macvlan/scripts/status.sh
examples/macvlan/scripts/down.sh

examples/ipvlan-l2/scripts/up.sh
examples/ipvlan-l2/scripts/capture.sh transform captures/ipvlan-transform.pcapng
examples/ipvlan-l2/scripts/down.sh

examples/ipvlan-l3/scripts/up.sh
examples/ipvlan-l3/scripts/status.sh
examples/ipvlan-l3/scripts/down.sh

examples/mixed-network/scripts/linux-up.sh
examples/mixed-network/scripts/status.sh
examples/mixed-network/scripts/linux-down.sh
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
examples/static-route-policy/scripts/demo.sh start
examples/static-route-policy/scripts/demo.sh verify
examples/static-route-policy/scripts/demo.sh apply-route
examples/static-route-policy/scripts/demo.sh clear-route
examples/static-route-policy/scripts/demo.sh stop
```

For each cycle, prove receiver-confirmed allowed delivery, receiver absence plus
an advancing named nftables counter for denied traffic, exact kernel-route
absence/presence for the route transition, readable mirrored capture, live GUI
state changes, and preservation of unrelated canary resources. Follow the
additional failure-injection contract in [`prompt/verifier.md`](../prompt/verifier.md).

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

A macOS pass establishes portable and Docker Desktop behavior only. A complete
Linux platform verdict consists of a successful `full` profile plus the
explicitly authorized `native-linux` profile and any feature-specific manual
evidence required by the change.
