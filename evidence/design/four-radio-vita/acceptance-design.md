# P6 integrated acceptance

Acceptance uses the maintained seven-application example, normal shared images,
and the ordinary `graphx example` lifecycle. The authored `iq-loss-jitter` action
uses the existing bounded netem scenario on processor ingress (the host end of
its owned data veth). It is never applied automatically. No configuration schema
or production launcher is added.

The baseline observes 180 seconds of streaming, all four rates, independent
VITA/spectrum headers, rational sample timestamps, burst markers, spectrum gap
maps, frequency error (at most half a bin), fragmentation and memory. Wire sample
epochs must agree. First emission may lag the scheduled epoch by at most the
existing 100 ms host tolerance; arrival skew is reported separately. Source skips,
observer drops and receiver gaps are separate counters. Mean emitted rate must
be within 2% of the requested rate over the bounded baseline; this is a measured
laboratory limit, not a real-time guarantee under arbitrary host contention.
Application RSS must remain below each authored memory limit and its last-half
peak may not exceed its first-half peak by more than 16 MiB. Capture remains
independent and bounded. A missed observation is not credited as passing evidence.
The privileged observer requests a bounded 4 MiB receive queue on its own socket,
without changing global Linux buffer limits, and requires zero kernel observer drops.

Fault checks apply/clear the owned loss/jitter scenario, inspect padded FFT output
and authenticated control continuity, then stop one identity-checked application
at a time. Radio, detector, processor and recorder cases use new containers. They
verify honest unavailable/stale results, surviving streams, unchanged identities,
no automatic restart and bounded whole-graph shutdown. An unrelated sentinel is
preserved. Recovery uses an explicit whole-graph down/up with new containers.

Browser checkpoints cover authenticated topology, network paths, detector logs,
capture and degraded state. The test leaves a bounded checkpoint for inspection
and always cleans up on completion or timeout. Logs display detected frequencies;
there is no claim of a separate live retuning or frequency-control UI.
Lima normalization includes the fixed Mac loopback origin for observation-only
login as well as graphs with control grants. Other targets retain their own console
origin; enabling observation never grants control.
Raw edges without packet observations retain unavailable metrics and connection
state, while capture links remain accessible. Default counters are not credited as
measured zero throughput or as evidence that radio control disconnected.

Fresh normal images are built once for this verification run. Offline contracts
and preparation precede the separately authorized privileged Lima run. P6 results
and the complete requirement matrix belong in `verification.md`; implementation
alone does not establish acceptance.

Native lifecycle observation also covers children exiting while executable metadata
is disappearing. A bounded read-only retry preserves the actual child exit status;
unknown or substituted live identities still fail closed. The ownership regression
exercises 64 rapid exits and rejects a mismatched live identity on macOS and Linux.
Scenario and ordinary lifecycle validation reconstruct container services from the
same owned ledger, including the recorder's mirror endpoint. Foreign graph/service
names and duplicate service bindings are rejected before endpoint resolution.
After explicit down, a ledger containing only retained history/capture volumes is
inactive. Status still verifies those volume identities and allows ordinary up to
recover the whole graph. Remaining containers, credentials, pending volumes or
network-resource intent cannot be classified as inactive.
