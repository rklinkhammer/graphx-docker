# P3 verification — jumbo paths and passive recorder

## User instructions: where to start

Use the [P3/P4 operator runbook](privileged-verification-runbook.md). It provides:

1. Existing image preparation and live harness commands.
2. Mac-side preflight, the current stopped/stale Lima prerequisite and repair boundary.
3. Exact verified artifact paths, graph identities and bounded resource selections.
4. Explicit authorization, execution, failure inspection and owned recovery steps.
5. Case matrices and evidence required for phase closure.

Private image verification and portable harness checks are recorded in
[P3/P4 preparation](p34-preparation.md). The live harness has not executed against
OVS. Preparation is P3/P4 work; P5/P6 and a separate Linux login are not prerequisites.

P3 is **not complete**: implementation and nonprivileged evidence are present;
actual Linux/Lima OVS path acceptance remains unrun. No privileged test, P4 policy,
image publication, push or deployment is authorized by this record.

Baseline: GraphX `2e0e8abc547a989b27fe732f4a50e32a35fe44f1`.
The sole VITA codec/runtime remains vrt_framework
`dbe85d37155145842da60367af1c4beef8801b0c`, checked locally at
`/Users/rklinkhammer/workspace/vrt_framework`. P1/P2 protocol implementations and
the unchanged start-epoch reproducer are preserved.

## Exit-criterion assessment

| Criterion | Implementation and portable evidence | Actual path evidence |
|---|---|---|
| Authoritative authored/normalized MTU and recorder contracts | Schemas, C++ model/loader, compiler and opt-in catalog updated together. Negative fixtures cover bounds/types, insufficient bridge MTU, dedicated mirror port, invalid owner/delivery and short snap length. | Pending live startup/rejection. |
| End-to-end jumbo paths | Both veth MTUs and OVS mtu_request configured; observed host, peer and OVS MTU checked before the application release barrier. TSO/GSO/GRO and transmit checksum offload are disabled/verified on both jumbo veth ends. Managed VITA paths require one owned bridge. | Pending maximum UDP delivery without fragmentation, including mirror. |
| Namespace mirror and identity | Container/namespace identity and both ifindices retained; alias matching is exact. Ledger tests reject replaced container, namespace and peer name, and retain interrupted-creation identity. | Pending move interruption, replacement refusal and actual cleanup. |
| Complete graph traffic and duplicate selection | Dedicated select-all mirror, isolated owned bridge; no protocol filter. One output per forwarding instance is the selected OVS contract. | Pending byte comparisons for IQ, Context, control/replies, spectra, ARP and duplicate-selection counts. |
| Separate bounded passive recorder | Linux application/catalog, common readiness/telemetry, fixed frame storage, bounded socket/log output, counters, capability drop and TSYNC receive-only filter. Linux tests verify receive succeeds while transmit, duplication, new sockets and writable opens fail. Compiler test requires NET_RAW only, all other capabilities dropped, no host network/socket mounts. | Pending actual AF_PACKET capability startup, raw injection refusal, tc enforcement, saturation and shutdown. |
| Independent diagnostic capture | Existing capture lifecycle owns a separate host diagnostic mirror endpoint; snaplen >= MTU+22 and bounded rotation/retention. Recorder writes no archives. | Pending independent on/off operation, full frame bytes, retention and recorder failure isolation. |
| Regression and lifecycle contracts | Native P1/P2 tests, common ownership/configuration suites, quality and portable gates; opt-in UBSan coverage and Linux receive-descriptor tests. | No privileged Linux, Lima, TCG or KVM evidence in this change. |

The catalog allows container execution for radio/processor/detector and requires it
for recorder. Its existing design image pins are **not runnable P3 image evidence**.
The verified private image/catalog is recorded in [preparation evidence](p34-preparation.md);
complete published images and the demonstration graph remain P5 work. This is an
unrun platform qualification prerequisite, not a claim of container application acceptance.

## Packet and storage budgets

| Traffic | UDP payload | IPv4 packet | Ethernet, no VLAN/FCS | Two VLAN tags, no FCS |
|---|---:|---:|---:|---:|
| Maximum VITA | 4128 | 4156 | 4170 | 4178 |
| Maximum P2 power FFT | 8836 | 8864 | 8878 | 8886 |
| Target IP MTU | — | 9000 | 9014 | 9022 |

FCS, preamble and inter-packet gap are separate wire overhead, not IP MTU or
ordinary AF_PACKET capture bytes. Diagnostic snap length is at least 9022 at MTU
9000. Recorder storage is a fixed 9022-byte frame buffer, an effective socket
buffer checked at <= 2 MiB, and one pending summary line. VLAN auxiliary metadata
is included in byte accounting. The recorder is best effort and reports kernel
drops; it neither archives packets nor promises lossless draining.

The **nominal IQ source payload is 16 MB/s** (four radios, one million complex
samples/s each, four bytes/pair). This is not measured Ethernet traffic or measured
recorder throughput. Neither live measurement has been made in this change.

## Executed commands

Host: macOS arm64; Apple clang 21 ordinary build. UBSan/static analysis: Homebrew
LLVM 21 with MacOSX26 SDK. Linux tests: local OrbStack Linux aarch64 verifier image
`sha256:e590ad0533b735bf7bd8555830c0416f4451e4bf5c74244df08ef7bc89eef5d6`
(GNU C++ 15.2), no network, all capabilities dropped, no privileged/socket mounts.
All commands below are run from the GraphX repository root. Local logs are retained
under `outputs/verification/p3-20260920/`; they are generated evidence, not maintained
source.

```sh
cmake --build build/vita-migration -j4
ctest --test-dir build/vita-migration \
  -R 'graphx-(ownership-state|config-network|vita)' --output-on-failure
python3 tests/test_vita_network.py build/vita-migration \
  --retain outputs/verification/p3-contract-fixture-20260920-final
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quick
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quality
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh portable
```

Native selected suite: **17/17 passed**, 74.83 seconds. Final focused ownership,
resource-module, recorder and network-contract checks: **4/4 passed**.

Repository quick: **41/41 passed**, 106 seconds including build. Portable:
**passed**, 117 seconds, including all 41 development tests, normalized consumers,
HTTP/web checks and owned native lifecycle. Repository quality: **passed**, 38 seconds.
Opt-in recorder clang-tidy/cppcheck: **passed**.
ShellCheck: **passed**. No diagnostics were disabled.

The retained fixture contains authored v3 input, private catalog lock and compiled
contracts. It runs configuration/compiler assertions only. It creates no containers,
interfaces, OVS objects or credentials. `--retain` requires a fresh directory.

```sh
/opt/homebrew/opt/llvm@21/bin/clang-tidy -p build/vita-migration \
  --extra-arg=-isysroot \
  --extra-arg=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk \
  src/vita/recorder.cpp tests/test_vita_recorder.cpp apps/vita-recorder/main.cpp
cppcheck --enable=warning,portability --inline-suppr \
  --suppress=missingIncludeSystem --error-exitcode=1 -Iinclude \
  src/vita/recorder.cpp tests/test_vita_recorder.cpp apps/vita-recorder/main.cpp
cmake --build build/vita-ubsan -j4
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/vita-ubsan \
  -R 'graphx-(vita|ownership-state|config-network|resource-modules|sanitizer-coverage)' --output-on-failure
UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir build/vita-ubsan \
  -R graphx-sanitizer-coverage --output-on-failure -V
```

Final UBSan suite: **19/19 passed**, 88.18 seconds.
The instrumentation audit passed for 50 library, 12 application and 20 test
translation units, including recorder. Build directories use the P1/P2 configuration
documented in [P2 verification](p2-verification.md). The final UBSan selection adds
the owner-scoped OVS query fixture and the audit to the VITA/ownership/network suite.

The timing suite is run separately from compilation. An overlapping-build run
failed the unchanged no-gap UDP timestamp assertion with a one-packet-sized gap;
the failure occurred while builds overlapped, and its cause was not isolated.
The isolated rerun passed; this remains a load-related qualification limitation. No assertion, scheduled sample epoch,
backend actual execution time or library behavior was changed to obtain a pass.
`shellcheck infrastructure/lima/provision.sh` checks the added `ethtool` package
prerequisite; provisioning was not run. No new VITA codec or
network-frame parser was introduced: recorder frames are opaque receive/discard
bytes. The fuzz profile was not rerun for P3. Existing P1/P2 library/adapter fuzz
qualification is unchanged; it does not
qualify the new tc/namespace path.

Linux receive-only tests were run with UBSan. The command was:

```sh
docker run --rm --network none --cap-drop ALL --security-opt no-new-privileges \
  --mount type=bind,src=/Users/rklinkhammer/workspace/graphx-docker,dst=/source,readonly \
  --mount type=bind,src=/Users/rklinkhammer/workspace/vrt_framework,dst=/vrt,readonly \
  --env GIT_CONFIG_COUNT=2 \
  --env GIT_CONFIG_KEY_0=safe.directory --env GIT_CONFIG_VALUE_0=/vrt \
  --env GIT_CONFIG_KEY_1=safe.directory --env GIT_CONFIG_VALUE_1=/source \
  --entrypoint bash e590ad0533b7 -lc '
    cmake -S /source -B /tmp/p3 -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DGRAPHX_BUILD_TESTS=ON -DGRAPHX_BUILD_VITA_RADIO=ON \
      -DGRAPHX_ENABLE_SANITIZERS=ON -DGRAPHX_SANITIZERS=undefined \
      -DFETCHCONTENT_SOURCE_DIR_YAML-CPP=/source/build/vita-migration/_deps/yaml-cpp-src \
      -DFETCHCONTENT_SOURCE_DIR_SOAPYSDR=/source/build/vita-migration/_deps/soapysdr-src \
      -DFETCHCONTENT_SOURCE_DIR_VRT_FRAMEWORK=/vrt &&
    cmake --build /tmp/p3 --target graphx-vita-recorder-test graphx-vita-recorder \
      graphx-ownership-state-tests graphx-resource-tests -j4 &&
    UBSAN_OPTIONS=halt_on_error=1 ctest --test-dir /tmp/p3 \
      -R "graphx-(vita-recorder|ownership-state|resource-modules)" --output-on-failure -V'
```

Final Linux UBSan: **3/3 passed**. The selection includes
`graphx-resource-modules`, which verifies owner-scoped queries against a fake OVS
executable containing two graphs with the same logical mirror ID. These use an ordinary loopback UDP socket for
syscall enforcement, including TSYNC denial in an existing worker thread and a 9014-byte receive; they do **not** open an
AF_PACKET socket or establish privileged path acceptance. Linux ASan, live OVS,
Lima, TCG and KVM qualification are unrun.

## P3 live case specifications

**Not executed.** Use the executable harness and numbered operator steps in the
[shared runbook](privileged-verification-runbook.md) before using these specifications.

Authorization must name native Linux or the dedicated GraphX Lima guest. Do not
run this plan in OrbStack, attach a physical uplink, forward a privileged socket,
or touch an existing graph. Keep runtime artifacts under `/var/lib/graphx`.

1. Build/verify a private test image and image-release catalog containing the
   current radio, processor, detector and recorder executables and their pinned
   dependencies. Reuse the existing image-release verification and CLI deployment
   contracts; do not use the design image pin as evidence. Record full image
   digests, host/guest architecture and application binary hashes. This image
   preparation and the live fixture adaptation are pending, not published P5 work.
2. Generate a fresh fixture with `tests/test_vita_network.py ... --retain ...`.
   Use a unique graph ID and free console port, an unused private subnet and one
   owned OVS bridge with no uplinks. Select the verified private catalog; regenerate
   its lock and compile through the authoritative loader. Preserve the four radio
   controls, four IQ inputs, spectra edge, passive recorder and optional independent
   diagnostic capture. Stage short-lived lab mTLS credentials through the existing
   credential mechanism. Preserve a separately owned sentinel workload throughout.
3. Capture before-inventories of Docker containers/networks/volumes, OVS bridges,
   ports/interfaces/mirrors, namespaces, links, qdiscs and capture processes. Start
   exclusively through the common CLI (the variables below identify the newly
   prepared fixture, not an arbitrary existing graph):

   ```sh
   graphx run up --output "$P3_COMPILED" --state-root "$P3_STATE" \
     --images "$P3_IMAGES" --release "$P3_RELEASE" --credentials "$P3_CREDENTIALS" \
     --allow-privileged
   graphx run status --output "$P3_COMPILED" --state-root "$P3_STATE" \
     --images "$P3_IMAGES" --release "$P3_RELEASE" --credentials "$P3_CREDENTIALS" \
     --allow-privileged
   ```

4. Read all expected interfaces and container identities from the ownership ledger.
   Verify both veth MTUs with `ip -d -o link` / `nsenter -t PID -n -- ip -d -o link`,
   OVS Interface `mtu` and `mtu_request`, Mirror `select_all`/`output_port`, and the
   exact `tc -j filter show dev HOST ingress` drop rule. Require `ethtool -k`
   to report TSO/GSO/GRO and transmit checksum offload disabled at both veth ends. Deliberately lower one
   owned endpoint MTU, verify status fails, and in a fresh startup verify no
   application release is published. Restore only the recorded owned endpoint.
5. Exercise maximum-size packets with IPv4 DF set using a test sender/receiver
   bound to the owned application interfaces; byte-compare 4128- and 8836-byte
   UDP payloads and prove no IP fragments. Independently observe matching source
   and mirror Ethernet frames (bounded in-memory hashes/lengths and diagnostic
   PCAPNG). Compare full frames including VLAN metadata where applicable.
   Check actual radio Data/Context, encrypted TCP commands/replies, spectra and ARP.
   Send uniquely numbered frames between two selected OVS ports; require exactly
   one mirror copy per forwarding instance despite both selection directions.
   Sentinel and management-only frames must be absent.
6. Inspect recorder effective/permitted/inheritable capabilities after readiness
   (all zero), seccomp state, mounts and networking. The recorder transmit probe
   must fail with EPERM; a separate test probe with NET_RAW on the recorded passive
   peer must be dropped by the host tc filter. Check its drop counter and absence
   on application ports. Do not send probes through the management interface.
7. Sustain the actual four-radio source, maximum FFT geometry and stalled recorder
   reads/log consumption for a bounded 60-second interval. Record actual source,
   wire and recorder counts separately from the nominal 16 MB/s IQ payload.
   Verify bounded RSS/socket/log storage, reported drops, and continued detector
   progress. Stop only the recorder after startup: radios/processor/detector must
   continue. Require bounded stop, no archive and no recorder-last dependency.
8. Repeat with diagnostic capture off and on; recorder counters must work in both.
   With capture on, verify full bytes, two-file/size/time retention bounds and
   continued capture during recorder failure. Attribute saved packets to GraphX
   diagnostics, never to the recorder application.
9. In fresh isolated runs, inject common lifecycle interruption after registered
   mutations and immediate process exit; recover through the ownership ledger.
   Include a peer move interruption and replaced namespace/ifindex/alias cases.
   Replacement must fail closed without deleting the foreign resource. Restore
   only test-created replacements before retrying owned cleanup.
10. Stop through `graphx run down` with the same options. Compare after-inventories
    against before: no new owned container, bridge, mirror, veth, namespace, qdisc,
    capture process or temporary state may remain; sentinel unchanged. Keep sealed
    capture/log evidence under the fixture root. On identity mismatch, retain the
    ledger and diagnose; never run broad Docker/OVS cleanup commands.

Live fixture/image preparation, all ten acceptance steps and their measured
results remain pending. P3 can close only after these criteria pass. No blocking upstream
vrt_framework defect has been identified in this work.
