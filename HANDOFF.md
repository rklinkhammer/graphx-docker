# GraphX chat handoff

Prepared for continuation on 192.168.1.202. This is a written context handoff,
not a native chat-session export. No pending coding task remains from P6.

## Repository state

- Source: /Users/rklinkhammer/workspace/graphx-docker on macOS ARM64.
- Branch: main.
- Required commit: ba710a3d4823e0507db59a5696b84bb9c0f769a9 (P5/P6 baseline).
- Working tree clean when packaged. P5/P6 changes are committed; no patch is needed.
- Prior baseline: 00d328991a03c3b21be58d96817ae9e4874f9273 (P3/P4 verification).
- The package contains context and selected evidence, not a repository checkout or images.

On the destination, locate the GraphX checkout and verify its HEAD against the commit
above before continuing. Fetch/update normally if needed; preserve destination changes.
Read AGENTS.md and docs/project-decisions.md first. Read
 design/four-radio-vita/implementation-plan.md and
 design/four-radio-vita/verification.md for the completed implementation and evidence.
Do not infer that P7 or another feature has been requested: obtain the user's next task.

## User preferences and authorization

- User requested rebuilding from the current repository without backups.
- Build fresh images once per verification run, then use new containers per case.
- Preserve identity-checked, fail-closed mutation/cleanup; avoid global prune/reset.
- Explicit P3/P4, P5 and P6 privileged verification was authorized in the original
  identity-matched GraphX Lima VM. It is complete and cleaned up. This does not
  authorize a new privileged run on 192.168.1.202 merely because it may be Linux.
- No messages to others, commits or pushes should be inferred from this handoff.

## Completed work

P5 promoted the four VITA node types and wire schemas into the authoritative catalog,
provided normal shared VITA images with qualification hooks OFF, and added the
maintained seven-application examples/four-radio-vita/graphx.yml. P6 added independent
wire measurements, bounded sustained/fault/failure/recovery verification, an explicit
owned iq-loss-jitter scenario, browser acceptance and operator documentation.

P6 also fixed issues discovered during verification:
- Process-exit metadata races preserve exact child exit status without trusting an
  unknown/substituted live identity.
- Observation-only Lima graphs allow the fixed Mac loopback console origin.
- Scenario validation shares ledger-derived container bindings with normal lifecycle,
  including the recorder mirror owner; foreign/duplicate bindings are rejected.
- Fully stopped graphs retaining only evidence volumes report inactive, permitting
  ordinary down/up recovery; incomplete cleanup still requires validation.
- Unobserved raw browser edges show unavailable metrics rather than invented zero
  counters/disconnected status; capture links remain available.
- The HTTP security test waits for actual readiness rather than liveness alone.
- The acceptance observer correlates context immediately preceding first IQ and uses
  a bounded owned receive buffer with zero observer drops required.

## Final acceptance

All five cases passed using one freshly rebuilt normal release: baseline, radio1,
detector, processor, recorder. Four failure cases also recovered with new containers.
- 180-second baseline: 999,351–999,424 samples/s per stream at 1,000,000 requested.
- First-arrival skew: 0.100 ms; first arrivals 0.537–0.638 ms after the common epoch.
- Max tone error: 195.3125 Hz, below half of the 488.28125 Hz FFT bin.
- Zero IP fragments; max UDP payloads 4,128-byte IQ and 8,836-byte spectrum.
- Peak application RSS: 26.1 MiB; no second-half peak growth.
- Owned loss/jitter: 25,883 reordered IQ packets, 1,368 padded spectra; all four
  authenticated controls continued. Both observers reported zero kernel drops.
- Recorder death did not stop diagnostic capture; two-file/4 MiB rotation bound held.
- Real Safari inspection covered auth, topology/OVS paths, frequencies in logs,
  capture catalog, degraded radio status and unavailable metrics.
- Quick: 43/43; quality and portable passed. Final portable took 176 seconds.
- No shell scripts were changed, so ShellCheck was not applicable.

Best-effort streaming still has source skips: 419 padded baseline spectra; end-of-run
source skipped-sample counts include the later fault interval. Zero observer drops
is not a claim of lossless application streaming. No physical radio, GPS discipline,
automatic retuning/restart, per-container recovery, persistent recorder, QEMU, TCG or
KVM acceptance is claimed. See the full verification report for precise limits.

## Evidence and original-host locations

The included evidence/outputs/verification/p6 JSON files are copied from actual test
results, not reconstructed. Source documents are included beneath evidence/ too.
To restore report-relative evidence links in the destination checkout, copy the two
JSON files into outputs/verification/p6 without replacing existing files silently.

Original guest (not automatically transferred):
- /var/lib/graphx/verification/p6/run6
- /var/lib/graphx/verification/p6/images-recovery
- /var/lib/graphx/verification/p6/cli/graphx
- /var/lib/graphx/verification/p6/examples
- /var/lib/graphx/captures and retained Docker history volumes.
Original host logs/artifacts: outputs/verification/p6.
Lima identity: 995153b13806694ef031f1b07218f9eafd2c31043deb33457504497cb153339e.
Network: 10.79.0.0/24; guest console 8080, Mac loopback 18080.

Final inventory: no containers, OVS bridges, namespaces, GraphX-owned links, netem,
GraphX/dumpcap processes or console listeners. Default Docker networks only. Sentinels
survived each graph operation and were then removed by exact identity. History and
capture evidence remain in the original guest. Verification browser tab was closed.

## Transfer status at preparation

SSH BatchMode to rklinkhammer@192.168.1.202 failed authentication. Confirmed destination: rklinkhammer@192.168.1.202:~/workspace/graphx-docker.
The local SSH agent has no identities and no default private keys are present.
Key authentication or an interactive user-run transfer is required. No remote files
have been written by this handoff.
