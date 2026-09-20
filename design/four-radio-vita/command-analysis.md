# GraphX command and capability contract

The selected VITA implementation is `vrt_framework`, explicitly configured with
its `graphx_radio` profile. The current standalone application requires migration
to its public Controller/controllee and host bindings; existing generated-code
tests are not evidence of this integration. No second VITA parser or encoder may
remain in GraphX after migration.

## Acknowledgments

- AckV reports validation/scheduling admission and the scheduled time. It never
  confirms execution. A client waiting to learn that a start was accepted requests
  AckV and continues waiting for AckX when execution evidence is required.
- AckX follows the device action and carries actual effective time. Successful
  activation is distinct from observing the first UDP packet at a receiver.
- AckS reports observation time and selected current values. EXECUTE requesting
  state must set ReqX+ReqS; the replies are AckX then AckS. EXECUTE+ReqS alone is
  unsupported. NO_ACTION+ReqS is the read-only query path.
- Replays retain original semantic results and timestamps. Retries must not
  restart acquisition, reapply settings or observe fresh state under an old ID.

This is the selected peer-profile interpretation of ANSI/VITA 49.2-2017 (R2024)
Rules 8.3.1.5-4/-7 and 8.4.1.5-4/-5. Table 8.4.1-1's SchX scheduling wording does
not authorize GraphX clients to interpret early scheduling acceptance as confirmed
execution. The supplied standard's Execution Timestamp rule uses the label AckE;
the framework's existing execution-evidence interpretation is retained.

## Configuration, status and capabilities

One correlated configuration carries RF frequency, sample rate, bandwidth and
gain. Validate the entire operation and `0 < BW <= Fs` before device mutation.
Raw CAM partial flags cannot permit partial configuration. Commit through the
same device owner used by SoapySDR acquisition. A failed commit must distinguish
no effect from unknown physical state; uncertain effects fault streaming.

Current-state queries select any of the four settings and streaming state;
full status selects all five. Supported-limit queries instead select the four
setting fields with CIF7 Maximum (bit27) and Minimum (bit26), mask `0x0c000000`.
CIF0 bit7 enables CIF7. AckS returns Maximum then Minimum per selected field;
Current is absent. Section 9 (printed p124) explicitly describes hardware-supported
sample-rate limits; Section 9.12 (pp219-220) defines the attributes.

Capability limits are global, available before configuration, while stopped,
armed and streaming. Bandwidth-versus-rate constraints are a separate bounded
profile hook and admission rule. Device steps/discrete choices are not CIF7
representational Precision and receive no invented wire selector. Unsupported
selectors produce explicit diagnostics when requested; no fabricated zero ranges.
Queries cannot change state, timing, phase or streaming.

## Wire and ownership

Data is type1, SID1-4, IQ16 big-endian I then Q, UTC/picoseconds, with Class ID
`00 FF FF FF 00 00 00 00`. Context and Command omit Class ID. Context is exactly
52 bytes carrying BW/RF/Gain/Fs. Data has a required SSI trailer, values
SINGLE=0/FIRST=1/MIDDLE=2/FINAL=3, at most1024 pairs per packet and262144 pairs
per burst. Configuration never starts IQ; a separate timed start does.

GraphX owns mutual TLS, authentication, socket deadlines, CS16 acquisition through
SoapySDR and graph lifecycle. The library owns Packet Size framing, codec validation,
correlation, replay, acknowledgments, sample timestamp arithmetic and publication
ordering. Authentication must bind reconnects to the same radio-lifetime operation
identity; reconnect cannot reset message-ID replay protection.

## Required migration evidence

Record the immutable library implementation commit, its requirement-to-evidence
matrix and exact platform/sanitizer results. Then replace generated packet classes
and GraphX's protocol engine with library APIs and device/transport adapters.
Update the controller to request AckV for admission and ReqX+ReqS for post-action
state. Add independent literal vectors for every layout, early-AckV/no-early-AckX,
actual/observation timestamps, execution-only suppression, atomic backend failures,
read-only capabilities, replay, fragmented/coalesced TCP, stalled peers and burst
boundaries. Keep library qualification distinct from Soapy/mTLS loopback and
privileged OVS deployment qualification. P1 remains open until all required host
integration evidence exists.
