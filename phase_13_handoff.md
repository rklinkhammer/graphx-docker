# Phase 13 implementation handoff

Date: 2026-09-06  
Implementation host: macOS 26.0, arm64, Docker Desktop Linux/arm64 engine 29.4.0  
Contract: `prompt/implement.md`  
Independent verifier contract: `prompt/verifier.md`

## Outcome

Phase 13 now provides one single-SDR suite with portable `simulated` and native-
Linux `external` profiles. Both model raw UDP IQ blocks from SDR to processor,
mutual-TLS TCP device control from processor to SDR, and raw TCP results from
processor to a distinct sink. Shared endpoints, payloads, packet observer,
telemetry, GUI, history, capture, and control semantics are reused.

The simulated profile completed a real Docker run on this macOS host. Live
packet counters advanced, the separate packet-history API returned records,
GUI pause/resume changed the SDR transmit state through processor mTLS control,
and an independently parsed Ethernet PCAPNG contained all three protocols/ports.
The native OVS/macvlan/netns profile was configuration-validated and dry-run
inspected only because this is not a native Linux host. Phase 13 therefore awaits
independent portable and native-Linux verification.

## Acceptance traceability

| ID | Implementation evidence | Validation evidence | Status / remaining action |
|---|---|---|---|
| SDR-001 | `examples/sdr-node/*/graphx.yaml`; bridge or OVS edge paths; external runtime support | Both configs validate; simulated `/api/topology` showed three nodes, bridge, and three raw edges | Implemented; inspect external GUI on Linux |
| SDR-002 | `common/protocol.py`, `sdr_simulator.py`, `processor.py`; UDP 18400 `framing: none` | Determinism/malformed tests; live processor results; PCAP payload began `SDR1` | Implemented |
| SDR-003 | TLS 1.3 mTLS server/client and four-command vocabulary; local seven-day CA/certs | Positive state/tune/start/stop plus missing-client, wrong-name, and range rejection tests; live control | Implemented |
| SDR-004 | One `common` implementation and generalized QEMU observer rules | QEMU static regression passed; both Compose files reference the same image/code | Implemented |
| SDR-005 | External macvlan, `br-sdr`, three veth ports, `mirror-sdr`, `sdr-cap`, namespace endpoint | `graphx infra create --dry-run` produced the intended commands | Partial: native Linux runtime required |
| SDR-006 | Observer emits signed `network_packet` from `ethernet-pcap` or `ovs-span`; telemetry accepts bounded known sources | Simulated counters advanced `0 -> 24`, later `776 -> 797`; telemetry tests passed | Implemented for API; browser/WebSocket visual check remains independent verification |
| SDR-007 | Source PCAP, link-type-1 PCAPNG, separate SQLite history, API proxy/catalog, default limits and disable flags | Packet API returned records; `capinfos` reported Ethernet, 5,497 packets, 745 kB; TShark decoded ports | Implemented; disable modes and native capture require independent runtime checks |
| SDR-008 | Existing GUI receives topology/runtime/control/capture/history metadata; SDR topology model test | Web tests 14/14; live API/control verified | Partial: interactive all-tab/browser verification required |
| SDR-009 | Consistent CLI, loopback-port preflight, bounded waits, state paths, fixed names, validated PIDs, failure trap, teardown | Simulated start/verify/stop and immediate replacement run passed; no SDR containers remained | Implemented portable; native interruption/restart/cleanup gate required |
| SDR-010 | Private networks, loopback GUI, mTLS keys mode 0600, narrow capabilities, bounded parser/storage/logs, no physical interface adoption | Static analysis, negative TLS/parser tests, Compose inspection | Partial until native listener/capability/process audit |
| SDR-011 | Additive external runtime and external-edge cycle rule; managed cycles remain rejected; QEMU defaults preserved | Full 34-test CTest (after focused fixes), QEMU static, telemetry, web, formatting, static analysis | Implemented for tested platform |
| SDR-012 | Suite and profile READMEs, ADR 0014, architecture, example index, graphical guide, test procedure, contracts | Documentation-consistency CTest passed | Implemented; verifier should execute commands literally |

## Important design decisions

`data_plane: graphx` remains acyclic. Only descriptive external raw edges are
excluded from scheduling-cycle detection, allowing the processor-to-SDR control
relationship to point opposite the sample stream. ADR 0014 records the reason,
consequences, ownership boundary, and rejected alternatives.

The portable bridge is explicitly a simulation, not OVS evidence. The native
profile owns a disposable namespace simulator and fixed lab resources. It never
attaches a physical NIC. A production SDR remains `runtime: external`,
`lifecycle: external`; the operator must review address, MAC/VLAN/MTU, routing,
firewall, certificate, RF safety, ownership, and rollback before manual use.

GUI Pause and Resume target the processor controller, which sends SDR stop/start
over mTLS. GUI Reset retains its existing collector-counter semantics and does
not reset the SDR. Direct `status/start/stop/tune` is exposed by the demo CLI.

`--no-capture` disables derived PCAPNG/catalog output but retains the bounded
classic PCAP needed as the passive observation source. `--no-history` disables
message and packet record retention. These semantics match the shared QEMU
packet-observer architecture.

## Commands and results

| Command | Evidence class | Result |
|---|---|---|
| `cmake --preset dev && cmake --build --preset dev -j 4` | Portable runtime/build | Passed after correcting the new edge wrapper access |
| `ctest --test-dir build/dev --output-on-failure` | Portable automated | Final run passed 34/34 in 16.19 s; an earlier ADR-count expectation was corrected for ADR 0014. |
| `python3 tests/test_sdr_example.py .` | Automated simulated dependency | Passed protocol, attribution, TLS identity/client-auth, state, range, and script syntax checks |
| `npm test --prefix apps/telemetry` | Portable automated | 76/76 passed |
| `npm test --prefix web` | Portable automated | 14/14 passed |
| LLVM 21 `scripts/check-format.sh` | Static check | 45 C++ files passed |
| LLVM 21 `scripts/run-static-analysis.sh` | Static check | clang-tidy and cppcheck passed all production/application/test targets |
| both `docker compose ... config --quiet` | Model validation | Passed |
| `graphx infra create external/graphx.yaml --dry-run` | Inspection only | Produced veth, OVS bridge/ports/mirror, and macvlan network plan |
| simulated `demo.sh start`, `verify`, GUI API control, `stop` on port 18083 | Portable runtime — macOS | Passed after fixing capture capability, telemetry history mount, packet source allow-list, and port preflight |
| `capinfos` and `tshark` against retained PCAPNG | Portable runtime — macOS | Valid Ethernet PCAPNG; all intended TCP/UDP ports decoded |
| architecture DOCX regeneration and packaged LibreOffice render | Documentation artifact QA | `docs/GraphX_Architecture.docx` regenerated from the maintained Markdown; all 20 rendered pages inspected with no clipping, overlap, broken tables, or missing glyphs. Connector layering in Figure 1 was corrected during review. |
| external `demo.sh start/verify/stop` | Not verified — environmental restriction | Native Linux, OVS, macvlan, and netns unavailable on this host |
| physical SDR run | Not applicable to Phase 13 exit | Optional; no hardware was attached or claimed |

## Native Linux operator gate

After reviewing `examples/sdr-node/external/README.md`:

```bash
scripts/verify.sh quick
examples/sdr-node/external/scripts/demo.sh start
examples/sdr-node/external/scripts/demo.sh status
examples/sdr-node/external/scripts/demo.sh verify
sudo ovs-vsctl show
sudo ovs-vsctl list mirror
sudo ip netns exec gx-sdr-device ip address show
examples/sdr-node/external/scripts/demo.sh stop
```

Repeat start/stop and independently confirm `br-sdr`, `gx-sdr-device`,
`gx-sdr-native`, all `sdr*-{ovs,parent,dev,cap}` interfaces, listeners, and owned
processes are absent. Inspect the retained PCAPNG and packet database. Run the
portable simulated profile on Linux separately; native success cannot substitute
for portable profile parity.

## Remaining verifier focus

- Exercise all GUI tabs and WebSocket updates in a real browser on macOS and
  Linux, including History → Network and Capture transitions.
- Test capture/history disable flags, record/file limits, PCAP replacement or
  truncation, malformed TLS/application requests, port/name collisions, forged
  PID state, and interrupted native startup.
- Audit native capabilities, addresses, routes, listeners, OVS mirror state,
  Docker isolation, exact cleanup, and repeated operation.
- Confirm the external lifecycle never mutates a physical interface and that
  simulator evidence is not relabeled as real SDR evidence.
