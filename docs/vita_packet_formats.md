# VITA packet formats and field-mapping reference

This is the common packet reference for the [four-radio system](vita_system.md)
and its [P1 radio implementation](../design/four-radio-vita/README.md). It summarizes
the selected GraphX profile, not every packet allowed by VITA 49.2. **Implemented**
means present in the current standalone radio; **selected** means an accepted design
choice; **open** means an encoding or behavior still needs resolution. Implementation
does not imply complete independent verification or standards certification.

The maintained generation input is [radio.yaml](../config/vita/radio.yaml).
Runtime behavior is in [radio.cpp](../src/vita/radio.cpp); independent wire tests are
in [test_vita_radio.py](../tests/test_vita_radio.py). Offsets below describe those
specific layouts. All offsets and lengths are bytes unless stated otherwise.

## Packet families and responsibilities

| Packet / generated class | Direction and transport | Purpose | State |
|---|---|---|---|
| Signal data: `RadioData` | Radio → IQ processor, UDP unicast | Signed complex IQ and sample timing; burst markers in trailer | Implemented |
| Context: `RadioContext` | Radio → IQ processor, same UDP destination | Applied center frequency, sample rate, bandwidth and gain | Implemented |
| Execute command: `RadioControl` | Controller → radio, TCP/mTLS | Configure, scheduled start, stop | Implemented |
| Read-only status command: `RadioQuery` | Controller → radio, TCP/mTLS | Select current settings/state to return | Implemented |
| Execution/error acknowledgment: `RadioAckVX` | Radio → controller, same TLS session | Scheduled/executed result or field errors | Implemented subset; validation-only mode is not exposed |
| Status acknowledgment: `RadioAckS` / `RadioQueryAckS` | Radio → controller, same TLS session | Selected applied values following execute/query, respectively | Implemented |
| Capability command and response | Controller ↔ radio, existing TCP/mTLS | Supported ranges and constraints | Architecture selected; fields and runtime endpoint open |

Context remains applied-state information. The selected capability mechanism is a
read-only VITA command/response, not an unsolicited capability-context stream.
The recorder receives mirrored frames without modifying any of these packets.
TLS command payloads remain encrypted in passive diagnostic captures.

## Common conventions

- Words are 32 bits; multibyte wire values use big-endian ordering.
- Packet Size is the entire VITA packet length in words, including its header,
  optional fields and any trailer. IP, UDP, TCP and TLS framing are outside it.
- A UDP datagram contains one complete VITA packet. TCP carries consecutive VITA
  packets framed by Packet Size, without an added length prefix or GraphX envelope.
- Stream IDs are radio indices 1–4. Controller ID is 1; controllee ID equals the
  target radio index. Command identifiers are 32-bit words, not UUIDs.
- Integer timestamps are 32-bit UTC-encoded seconds; fractional timestamps are
  64-bit picoseconds within the second, less than `10^12`. This is simulated time,
  not measured GPS discipline. Data timestamps identify the first IQ sample.
- Class ID is zero and unused for dispatch. **It is present as eight zero bytes in
  IQ data; it is absent in the current context and command/ack definitions.**
- The data packet count advances modulo 16. It is not a sample count or burst ID.
  The runtime does not maintain corresponding advancing context/command counts;
  commands instead use message IDs for correlation and replay handling.

### First header word

Bit numbers are within one 32-bit word, with bit 31 most significant.

| Bits | Meaning in this profile |
|---|---|
| 31–28 | Packet type: `1` signal data with stream ID; `4` context; `6` command, including acknowledgments |
| 27 | Class ID present |
| 26 | Data: trailer present. Command: acknowledgment flag. Interpret by packet type. |
| 24 | Command cancellation flag; cancellation is not implemented |
| 23–22 | Integer timestamp selector: UTC in this profile |
| 21–20 | Fractional timestamp selector: real-time/picoseconds in this profile |
| 19–16 | Four-bit packet count |
| 15–0 | Total packet size in 32-bit words |

Other packet-type-specific bits are governed by the generated profile. This table
is not a license to accept arbitrary reserved bits or unsupported layouts. Validate
type, flags and complete declared length before accessing optional fields.

## IQ data packet

| Offset | Length | Field / mapping |
|---|---|---|
| 0 | 4 | Header: type 1, class ID present, trailer present |
| 4 | 4 | Stream ID 1–4 |
| 8 | 8 | Zero class identifier |
| 16 | 4 | Integer seconds |
| 20 | 8 | Fractional picoseconds |
| 28 | `4 × N` | `N` IQ pairs: signed 16-bit I followed by signed 16-bit Q |
| `28 + 4 × N` | 4 | Trailer with enabled sample-frame indication |

`1 ≤ N ≤ 1024`; no empty data packet. Total length is `32 + 4 × N` bytes.
A full packet is **4,128 bytes / 1,032 words**, carrying 4,096 IQ bytes.
A two-pair final packet is 40 bytes / 10 words.

```text
N = packet_size_words - 7 header words - 1 trailer word
```

This subtraction is specific to the implemented data layout; a parser supporting
other layouts must derive their actual header/trailer lengths. No separate sample
count is inserted into a VITA header or control CAM field.

The trailer's sample-frame value is SINGLE=0, FIRST=1, MIDDLE=2, FINAL=3, with its
enable indication set by the generated codec. The current value occupies trailer
bits 11–10. One-packet bursts use SINGLE. A maximum burst contains 262,144 pairs,
1 MiB of IQ payload and 256 full packets. Short final packets carry only valid IQ;
the source does not add padding. Consecutive bursts preserve sample time and phase.

Sample amplitude is quantized to signed 16-bit components; the default tone uses
0.25 full-scale peak amplitude before gain. Missing-sample zero insertion belongs
to downstream FFT processing. Burst identity, missing-boundary recovery and richer
quality indicators require further profile/receiver work; the current SSI marker
does not provide a separate monotonically increasing burst identifier.

## Applied context packet

| Offset | Length | Field |
|---|---|---|
| 0 | 4 | Header, type 4; no class ID or trailer |
| 4 | 4 | Stream ID |
| 8 | 4 | Integer seconds |
| 12 | 8 | Fractional picoseconds |
| 20 | 4 | CIF0, selected setting mask `0x28a00000` |
| 24 | 8 | Bandwidth |
| 32 | 8 | RF reference frequency, mapped to receiver center frequency |
| 40 | 4 | Gain stages |
| 44 | 8 | Sample rate |

Total: **52 bytes / 13 words**. Normal streaming emits context before the first
data packet and at burst boundaries. Context timestamps refer to the corresponding
sample position. Broader context behavior after pacing losses remains acceptance
work. The current packet does not advertise capabilities, payload-format descriptors,
simulated lock indicators or a settings-generation identifier.

### Shared setting encodings

These encodings are shared by context, execute settings and returned status.

| Setting | CIF0 selector | Encoding | Device meaning / accepted range |
|---|---|---|---|
| Bandwidth | Bit 29 | Signed 64-bit fixed point, 20 fractional bits, Hz | 1 Hz through the requested/applied sample rate |
| RF reference frequency | Bit 27 | Signed 64-bit fixed point, 20 fractional bits, Hz | Receiver center: 1 MHz–6 GHz |
| Gain | Bit 23 | One word: stage 2 in upper 16 bits, stage 1 in lower 16; signed fixed point with 7 fractional bits, dB | Stage 1: −60 to +60 dB; stage 2 must be zero |
| Sample rate | Bit 21 | Signed 64-bit fixed point, 20 fractional bits, samples/s | Integral 1,000–2,000,000 samples/s |

For the 64-bit fields, physical value = signed integer / `2^20`; gain stages use
`2^7`. Representational precision is not the same as supported device increments.
These field types/selectors are confirmed by the pinned
[vrtgen CIF0 model](https://github.com/Geontech/vrtgen/blob/5e7497d24069c140be431d8468655f67d25f382d/src/vrtgen/parser/model/cif0.py).

## Command and acknowledgment layouts

### Common command prologue

| Offset | Length | Field |
|---|---|---|
| 0 | 4 | Header, type 6; acknowledgment flag only for replies |
| 4 | 4 | Stream ID |
| 8 | 4 | Integer seconds |
| 12 | 8 | Fractional picoseconds |
| 20 | 4 | Control/Acknowledge Mode (CAM) |
| 24 | 4 | Message ID |
| 28 | 4 | Controllee ID |
| 32 | 4 | Controller ID |
| 36 | Variable | Selectors, values or error indicator fields according to subtype |

No class ID or data trailer is present. Successful execution replies can end at
offset 36. Status replies have CIF selectors followed by selected values. Error
replies use error indicator fields, not a status-value payload.

### Implemented operation layouts

| Operation | Bytes after prologue | Total bytes |
|---|---|---|
| Configure | CIF0=`0x28a00000`, bandwidth, RF frequency, gain, sample rate | 68 |
| Start / stop | CIF0=`2`, CIF1=`64`, discrete-I/O word | 48 |
| Query CIF0 settings | Nonempty CIF0 selectors only; no setting values | 40 |
| Query including streaming state | CIF0 with bit 1 set, CIF1=`64`; no discrete-I/O value | 44 |
| Status: sample rate only | CIF0=`0x00200000`, sample-rate value | 48 |
| Status: four settings | CIF0=`0x28a00000`, four values | 68 |
| Status: four settings plus state | CIF0=`0x28a00002`, CIF1=`64`, four values, discrete-I/O word | 76 |
| Successful execution acknowledgment | No status/error fields | 36 |
| One CIF0 field error | EIF0 selector, one error word | 44 |
| Discrete-I/O error | EIF0 enabling EIF1, EIF1 selector, error word | 48 |

The current maximum accepted command size is 1,024 bytes. Each packet in a
multi-packet response has its own Packet Size and shared operation correlation ID.
Stream enable is a **profile-defined mapping inside VITA discrete I/O**:
CIF0 bit 1 enables CIF1, CIF1 bit 6 selects discrete-I/O-32, and the value word uses
bit 1 as enable/valid and bit 0 as streaming value. Start encodes `3`; stop encodes
`2`. This is not a universal VITA start opcode.

### CAM and operation semantics

| Field | Location / selected meaning |
|---|---|
| Controllee/controller presence | CAM bits 31/29 set; format bits 30/28 clear for word identifiers |
| Partial application | Bit 27 clear; configuration is atomic |
| Action mode | Bits 24–23: NO_ACTION=0 for query, EXECUTE=2 for settings/start/stop |
| Execution request / acknowledgment | Bit 19 |
| Status request / acknowledgment | Bit 18 |
| Error request / acknowledgment | Bit 16; current execute profile requests errors |
| Timestamp control | Bits 14–12: IGNORE=0, DEVICE=1 for scheduled start; TIMING_ISSUES=7 in late-start error response |
| Scheduled-or-executed acknowledgment | Bit 10; scheduling acceptance is not proof of emitted samples |

Configure requires all four settings and a stopped, unarmed device. Configuration
alone emits no IQ. Start is separate, with a common simulated first-sample time
mapped to the host deadline; the current service accepts 20 ms–10 s lead. Stop is
immediate under IGNORE timing. Ordinary status is NO_ACTION with explicit selectors.
Both execution and status requests produce execution then status packets; an
execution-only request must not produce unsolicited status.

Message IDs are nonzero, increasing and scoped to the radio process. Exact duplicate
requests replay cached responses without reapplying device operations. The current
cache holds 256 entries; changed or expired IDs are rejected. Query selectors have
no execute payload and must be decoded by the query decoder.

### Errors and result interpretation

Errors identify the affected field using EIF masks. Implemented reasons include
field-not-executed (error word bit 31), out-of-range (28), unsupported precision
(27), invalid field value (26) and timestamp problem (25). Multiple invalid settings
can produce multiple field error words. A rejected command leaves applied settings
unchanged. Malformed framing/unsupported layouts may close the session instead of
producing a VITA error response. Full malformed/state-error coverage is still open.

Status values, execution acknowledgment and capability limits answer different
questions. Status is the applied state; an execution result describes the command;
capabilities describe what settings may be supported. Before configuration, device
defaults reported in status are not evidence that initialization has succeeded.

## Capability field-mapping decisions

The selected exchange is read-only VITA command/response over existing TCP/mTLS.
It must work before configuration and while stopped or streaming, without changing
settings, phase, timestamps or acquisition state. The following are **open mapping
decisions**, not already assigned wire fields:

| ID | Information needed | Candidate / decision to resolve |
|---|---|---|
| CAP-01 | Distinguish capability request from applied-status request | Verify explicit selectors/attribute combinations and reply subtype; retain correlation and authentication |
| CAP-02 | Supported minimum/maximum center frequency, rate, bandwidth and gain | CIF7 min/max attributes are candidates; establish supported-limit semantics, not statistical extrema |
| CAP-03 | Supported increments or discrete choices | Decide encoding for integral rates and any other setting steps; CIF7 precision is not automatically a device increment |
| CAP-04 | Cross-field constraints | Represent bandwidth versus rate; distinguish global limits from limits at current/proposed settings |
| CAP-05 | Unsupported capability selector / unavailable capability | Define bounded negative/unsupported response behavior without inventing successful zero-valued limits |
| CAP-06 | Capability changes / freshness | Decide whether the fixed virtual device needs only per-runtime static capabilities, and how changes would be identified |

The pinned generator models CIF7 current, minimum, maximum, precision and other
attributes. A reproduced limitation prevents automatic CIF7 inheritance into a
derived query acknowledgment. If CIF7 is selected, fix/test generation and independent
wire decoding; this is an implementation limitation, not a prohibition on VITA
capability queries. No capability packet size or final binary layout is assigned yet.
See [command analysis](../design/four-radio-vita/command-analysis.md) for the probe.

## Other mapping and verification items

| ID | Item | Current boundary |
|---|---|---|
| MAP-01 | Context/command packet-count progression | Only data count advancement is implemented; define and verify other families before relying on them |
| MAP-02 | Context association and loss recovery | Stream ID and time are present; receiver missing-context and change-boundary handling remain to be completed |
| MAP-03 | Burst discontinuity / missing boundaries | SSI and sample timestamps exist; no explicit burst ID is transmitted |
| MAP-04 | Data quality / clipping indication | Clipping is counted by the device; no dedicated clipping flag is currently populated in VITA data/context |
| MAP-05 | Configured, armed and running status | Discrete streaming state is returned; separate configured/armed status mappings remain unspecified |
| MAP-06 | FFT output | Power-spectrum messages belong to P2; they are not assigned a VITA packet type by this reference |

Future physical GPS discipline, automatic retuning and persistent recording are
outside the current standalone packet implementation. Do not encode them implicitly
by changing the meaning of an existing field.

## Sources and change discipline

Upstream reference: vrtgen revision `5e7497d24069c140be431d8468655f67d25f382d`.
Primary implementation references include its
[header model](https://github.com/Geontech/vrtgen/blob/5e7497d24069c140be431d8468655f67d25f382d/src/vrtgen/parser/model/base.py),
[command model](https://github.com/Geontech/vrtgen/blob/5e7497d24069c140be431d8468655f67d25f382d/src/vrtgen/parser/model/command.py),
[CIF7 model](https://github.com/Geontech/vrtgen/blob/5e7497d24069c140be431d8468655f67d25f382d/src/vrtgen/parser/model/cif7.py)
and [command example](https://github.com/Geontech/vrtgen/blob/5e7497d24069c140be431d8468655f67d25f382d/examples/packets/example-control.yaml).
These source findings do not replace the normative VITA standard.

Resolve CAP/MAP items here with the chosen meaning, encoding, rationale and evidence.
Then update packet definitions, runtime, independent vectors and
[P1 verification](../design/four-radio-vita/p1-verification.md) together. Do not
mark a mapping implemented solely because it appears in this reference. Existing
independent tests cover selected data/command layouts; context and capability
coverage remain incomplete.
