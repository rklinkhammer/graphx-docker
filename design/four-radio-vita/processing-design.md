# P2 processing design

Status: production native applications implemented; acceptance results are in
[p2-verification.md](p2-verification.md).

The native opt-in `vita.processor` owns four separately bound UDP inputs and four
ControllerSession instances. `vita.detector` consumes a distinct raw UDP power
spectrum schema. All endpoint, credential and parameter bindings use the existing
normalizer and node lifecycle. P3 adds optional container execution and managed jumbo attachments; live path
qualification is pending. P5 publication remains deferred.
The VRT pin remains dbe85d37155145842da60367af1c4beef8801b0c.

## Samples and control

Use the library's graphx_radio receiver profile, Context association and IQ16
codec. Validate the resolved source address and expected stream ID independently
on each input. Never parse VITA fields with a second GraphX codec. A per-stream
bounded sample assembler accepts decoded samples in timestamp order, tolerates
10 ms reordering and rejects duplicates. Unexpected sample-rate metadata is rejected against the authored FFT rate.
An RF metadata change discards an incomplete window, counts discarded samples
and preserves the original rational sample clock; late old-context packets cannot
roll that clock backward.
The common scheduled start seeds the sample epoch even if initial packets are lost. Windows span bursts and
short packets. At most 32 pending 1024-pair packets and one FFT window are
retained per stream. A confirmed gap is zero filled only within bounded windows;
an outage exceeding two windows resets the assembler and records skipped/discarded samples
instead of manufacturing an unbounded sequence of zero spectra.

Bind inputs before readiness. Configure the four radios independently within a
five-second window, then schedule successful radios at one UTC time 250 ms ahead.
Only terminal AckX confirms execution; report each start outcome. AckS verifies
the four configured values before scheduling. Keep sessions across socket
reconnects, query current state, and do not reconfigure/restart on ordinary reconnect.
A failed stream cannot block healthy streams. Credentials, authentication, partial
I/O, deadlines and replay continue through the P1 host transport boundary.

## FFT and detector

Use a bounded iterative radix-2 complex FFT in GraphX's signal-processing module;
no extra FFT dependency is required. Support N=64,128,256,512,1024,2048, all complex
bins in fftshift order, offsets [-Fs/2, Fs/2). Requested bin spacing must equal
Fs/N exactly. Windows are rectangular or periodic Hann. Overlap is an authored
percentage selected from 0,50,75; hop is N*(100-overlap)/100 samples.
IQ codes normalize by 32768. Bin power is |FFT(window*IQ)|^2/(sum(window)^2).

The detector selects the largest finite positive bin, with ties resolved to the
lowest frequency. RF frequency is center+(peak-N/2)*Fs/N; nominal error is at most
half a bin for isolated stationary tones. Zero spectra and windows containing gaps
produce explicit no-detection with quality metadata. Invalid messages produce an
invalid result and counter, never a fabricated frequency.

## Power wire format and bounds

`VitaPowerSpectrum` is raw UDP, distinct from TCP RawPowerResult. Use network byte
order and IEEE754 binary32 power bins. No GraphX envelope is added. A fixed
128-byte versioned header carries magic/version/header/total lengths, stream ID,
sequence, FFT size/hop/window, normalization/axis identifiers, RF frequency and
sample rate, start UTC/picoseconds, end-exclusive UTC/picoseconds, valid samples,
gap count, a 64-bit skipped/discarded-sample count, the sample epoch and cumulative window
ordinal (preserving fractional-period carry at non-divisor rates). A 64-bit source packet-count discontinuity counter completes the header.
An N-bit gap bitmap follows (one bit per missing sample), then an N+1-bit
observed-burst-boundary bitmap padded to N/8+4 bytes, then N power bins.
Bit zero is the most significant bit. Boundary bit i marks the boundary before
sample i; bit N marks the end of the window. FIRST/SINGLE marks before its first
sample; FINAL/SINGLE marks after its last. These are observed boundaries, not
invented burst IDs. Missing boundary packets leave unknown boundaries; sample gaps
remain explicit. No incomplete-burst object is retained or waited on: only the
10 ms packet reorder deadline applies. A short packet alone never implies a gap.
Boundary padding and the reserved header byte must be zero.
Total bytes = 132+N/4+4*N: 404,676,1220,2308,4484,8836 for supported N. Including
IPv4/UDP, maximum is 8864 bytes; require configured path MTU >=8864 for N=2048
(and the corresponding bound for smaller N, with an authored floor of 4200
bytes to accommodate the IQ inputs). Actual OVS no-fragmentation acceptance
remains P3. Validate size bounds before allocation and the entire message before
detection.

Nominal per-stream output rate is Fs/hop, four-stream rate 4*Fs/hop. Bound output
work and queues; report discards explicitly. Node-console output is nonblocking
and bounded: each received spectrum yields a detection/no-detection record or a
counted display drop, with per-stream/time identity. Do not silently sample away
results. Existing bounded Node console retention is the initial display.

## Qualification

Independent literal power-wire vectors and numerical oracles cover every N,
positive/negative tones, window normalization, overlap, bursts, short packets,
loss/reorder/duplicates, malformed metadata, all-zero spectra and prolonged outage.
Run the actual four P1 radios, production processor and detector over local
sockets using normalized private-catalog fixtures. Keep P1 regressions and run
quick, quality, portable and opt-in sanitizer checks. Record measured output
counts/rates, dropped work, commands and platform scope in p2-verification.md.

### Header offsets (bytes)

| Offset | Field | Encoding |
|---|---|---|
| 0 | `GXP2` magic | u32 |
| 4, 6 | version 1, header length 128 | u16 each |
| 8, 12, 16, 20, 24 | total length, stream 1–4, sequence, N, hop | u32 each |
| 28–31 | window (0 rectangular/1 Hann), normalization 1, axis 1, reserved 0 | u8 each |
| 32, 40 | RF center Hz, sample rate pairs/s | u64 each |
| 48, 56, 64, 72 | begin seconds/picoseconds, end-exclusive seconds/picoseconds | u64 each |
| 80, 84 | valid and missing sample counts (sum N) | u32 each |
| 88 | cumulative skipped/discarded samples outside emitted gap maps | u64 |
| 96, 104, 112 | epoch seconds/picoseconds, cumulative window ordinal | u64 each |
| 120 | source Data packet-count discontinuities after timestamp reorder | u64 |
| 128 | N-bit gap map | N/8 bytes |
| 128+N/8 | N+1-bit observed boundary map, zero padding | N/8+4 bytes |
| 132+N/4 | shifted power bins | N network binary32 values |

Packet counts wrap modulo 16; their discontinuity counter is diagnostic, not an
exact count of missing packets. Timestamps determine missing sample positions.
Spectrum sequence wraps modulo 2^32 independently per stream. The detector counts
forward sequence gaps and duplicates/reordered results separately and processes
every received valid result. Epoch plus cumulative ordinal retains fractional
sample-period carry; the receiver verifies both interval endpoints using the
library SampleTimeline. No VITA timestamps are rewritten.

### Authored parameters and bounded host work

For each radio i=1..4, `ratei`, `centeri`, `bandwidthi`, `gain_dbi` select its
atomic configuration. Gain is signed integer dB (-60..60). Node-type parameter
definitions now permit signed integer defaults/limits; each type still imposes its
own minimum/maximum. `bin_width_numerator_hzi / bin_width_denominatori` selects
exact rational Hz spacing. Defaults 15625/16 at 1 MHz select N=1024. For a rate
of 1000003 and N=2048, use numerator 1000003 and denominator 2048.
`window` is 0/1; `overlap_percent` is 0/50/75; `path_mtu` defaults to 9000.
The authoritative loader and normalized-node loader reject unsupported rational
spacing, bandwidth above rate, insufficient MTU/datagram bounds, non-unicast raw
bindings and unauthenticated control. The opt-in catalogs live in `config/vita/`;
normalization fixtures use a private locked catalog. No image is published in P2.

Per input: at most 32 queued packets of 1024 complex samples, 128 bounded header
hints for library Context deferral, and N buffered samples. The VITA runtime owns
its existing bounded Context history and deferred payloads. There is no queue of
spectra: UDP sends succeed immediately or increment a drop counter. Reads are
limited to 16 datagrams per stream per event-loop turn; assembler work is bounded
by its 32-packet queue and at most 2N zero insertions per accepted packet. All four
streams are serviced each turn. Decoder work is limited to 32 datagrams/turn.
Socket receive/send buffers honor normalized bounds. The display queue holds at
most 64 bounded lines and writes at most 16 chunks/turn. A stalled console yields
counted drops and recovers without blocking packet processing. Summaries report
input/metadata errors, duplicate packets, sequence discontinuities, skipped
samples, send drops and display drops. Existing Node console retention and
credentialed telemetry heartbeats remain in use. Sustained zero-loss throughput
at every maximum-rate combination is not claimed; counts expose overload.
