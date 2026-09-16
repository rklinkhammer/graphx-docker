# P1.1 — VITA command analysis

Status: analysis complete; command implementation and P1 acceptance remain open.
Scope: the selected vrtgen revision and the standalone radio. Status selectors,
requested acknowledgment modes and supported-setting errors are now implemented
from these findings. Capability discovery and broader P1 acceptance remain open;
the accepted four-radio architecture is unchanged.

## Conclusion

The upstream command examples provide useful configuration and stream-control
patterns. Status queries also have explicit generator support. Capability discovery
needs more work: CIF7 provides minimum/maximum attributes, but neither the inspected
example nor the inspected tests establishes that those attributes describe supported
device limits rather than other parameter attributes. Do not infer that semantic
contract from field names alone.

A concrete dependency limitation was reproduced: the parser accepts CIF7 on a
control query, but its derived acknowledgment does not inherit CIF7. Thus adding
minimum/maximum fields to our YAML alone is insufficient. This is a gap in the
candidate capability-response path, not a blocker for configuration, status,
start/stop or the remaining independent P1 tests.

## Evidence inspected

Upstream source revision: `5e7497d24069c140be431d8468655f67d25f382d`.
The local checkout's `git rev-parse HEAD` matched this revision.

| Source | Relevant evidence |
|---|---|
| [Packet example](https://github.com/Geontech/vrtgen/blob/5e7497d24069c140be431d8468655f67d25f382d/examples/packets/example-control.yaml) | CIF0 bandwidth, RF reference frequency, gain and sample rate; CIF1 stream enable; acknowledgment derived with `responds_to`. Its partial-application/warning policy is not our selected policy. |
| [Example controller](https://github.com/Geontech/vrtgen/blob/5e7497d24069c140be431d8468655f67d25f382d/examples/app/example_controller.cpp) | Sends settings and stream-enable commands, requests execution acknowledgment, reads field warnings. Does not demonstrate a capability-range query. |
| [Example controllee](https://github.com/Geontech/vrtgen/blob/5e7497d24069c140be431d8468655f67d25f382d/examples/app/example_controllee.cpp) | TCP listener using the generated controllee. Its lifecycle/transport does not supply GraphX mTLS, ownership or device serialization. |
| [Command model](https://github.com/Geontech/vrtgen/blob/5e7497d24069c140be431d8468655f67d25f382d/src/vrtgen/parser/model/command.py) | CAM request/ack fields, required status request for NO_ACTION, CIF0/1/2 inheritance for status acknowledgments; no corresponding CIF7 copy in that inheritance path. |
| [CIF7 model](https://github.com/Geontech/vrtgen/blob/5e7497d24069c140be431d8468655f67d25f382d/src/vrtgen/parser/model/cif7.py) | Current, maximum, minimum, precision and other attributes; enabled attributes are required, not individually optional in this model. |
| [C++ generator](https://github.com/Geontech/vrtgen/blob/5e7497d24069c140be431d8468655f67d25f382d/src/vrtgen/backend/cpp/generator.py) | NO_ACTION controls use CIF enable functions; EXECUTE/DRY_RUN and status acknowledgments use value functions. |
| [Command test definitions](https://github.com/Geontech/vrtgen/blob/5e7497d24069c140be431d8468655f67d25f382d/tests/codegen/yamls/command.yaml) | Active request/acknowledgment variants; the richer `SampleControlQ` query example is commented out and is not executable test evidence. |
| [Context byte tests](https://github.com/Geontech/vrtgen/blob/5e7497d24069c140be431d8468655f67d25f382d/tests/codegen/cpp/test_context.cpp) | Active CIF7 current/mean attribute byte checks; these do not establish command capability semantics. Several broader CIF7 cases are commented out. |
| [Enums](https://github.com/Geontech/vrtgen/blob/5e7497d24069c140be431d8468655f67d25f382d/include/vrtgen/packing/enums.hpp) and [command packing](https://github.com/Geontech/vrtgen/blob/5e7497d24069c140be431d8468655f67d25f382d/include/vrtgen/packing/command.hpp) | Distinguish NO_ACTION, DRY_RUN, EXECUTE; timestamp execution modes; acknowledgment action-scheduled-or-executed flag. Comments cite VITA tables but are not the normative standard. |

Workspace comparison: [packet definitions](../../config/vita/radio.yaml),
[radio service](../../src/vita/radio.cpp),
[independent harness](../../tests/test_vita_radio.py) and
[template adapter](../../scripts/vita/generate.py).

## Command mapping and implementation corrections

| Operation | Selected mapping / direction | Current gap or required verification |
|---|---|---|
| Initial configuration | Controller → radio; EXECUTE, complete CIF0 RF frequency/sample rate/bandwidth/gain; no partial application | Atomic validation is preserved. The service returns execution and/or status replies as requested; both requests yield two packets. Independent tests check applied values and execution/status bits. |
| Applied status | Controller → radio; NO_ACTION plus requested status, selecting supported CIF fields; radio → controller status acknowledgment | `RadioQuery` now has explicit selectors and its own generated acknowledgment. Selector-only lengths are validated separately from execute payloads; empty queries are rejected. Independent tests check selected and full status responses. |
| Capability/range query | Read-only query and response, with supported limits sourced from the same device owner | CIF7 is only a candidate. Verify allowed-limit semantics, fix/test acknowledgment generation if selected, then add independent bytes and no-mutation tests. Status values are not capabilities. |
| Scheduled start | EXECUTE with enabled stream-enable=true, DEVICE timestamp-control mode, common seconds/picoseconds start | Keep configuration separate. Current scheduled/executed flag records acceptance for scheduling; it must not be presented as proof that emission has begun. Independently verify response CAM bits, late errors, duplicate behavior and first sample time. |
| Stop | EXECUTE with enabled stream-enable=false, IGNORE timing | Keep listener alive and report actual stopped state. Verify requested execution acknowledgment and subsequent status. |
| Rejected setting | Negative acknowledgment with field-specific error indicators | Range and precision failures identify the affected setting; late starts identify a timestamp problem. Independent tests verify field masks/reasons and unchanged applied settings. Broader state/malformed-operation coverage remains. |
| Replay/reconnect | Correlated message ID, controller/controllee and stream identity, bounded runtime-scoped replay | This is service behavior, not supplied by the packet example. Verify conflicting duplicates, evicted IDs and reconnects without reapplication. |

Keep controller/controllee identifiers as the selected 32-bit words and stream IDs
1–4; the example's UUID controller identifier does not mandate changing our profile.
Retain mTLS and packet-size framing over TCP. The example does not justify adopting
its unauthenticated transport or allowing partial settings to apply.

Frequency, bandwidth and sample rate use the selected generated field encodings;
gain uses the generated stage fields. Independent tests must check byte order,
fixed-point scale, precision rejection and gain stage selection. A successful
matching encoder/decoder round trip is insufficient.

## Capability scope

Do not equate numeric range discovery with discovery of every operation, waveform,
format or cross-field constraint. The immediate P1 range scope is RF center
frequency, sample rate, bandwidth and gain. A maximum bandwidth also depends on the
applied sample rate; a response must distinguish a global device limit from a
currently valid setting. Integer sample-rate restrictions likewise need explicit
handling; a field named precision does not automatically mean supported step size.

The inspected examples establish no complete generic capability service. Before
implementing the candidate, obtain an authoritative specification passage or
independent interoperable profile/vector establishing supported-limit semantics.
If CIF7 cannot express this contract as intended, report that precise finding before
introducing extension fields or replacing the requirement with a static document.
No new product preference is needed for the other command corrections.

## Reproduced parser findings

This unprivileged probe uses the pinned installed dependency, without the workspace
template adapter. It changes no source or generated packet files:

```sh
build/vita-tools/bin/python - <<'PY'
import yaml
from vrtgen.parser.loader import get_loader
profile = '''Query: !Control
  cam: !ControlAcknowledgeMode
    req_s: true
    action_mode: none
  cif_0: !CIF0
    sample_rate: required
  cif_7: !CIF7
    min_value: required
    max_value: required
Reply: !Ack
  responds_to: Query
'''
for name, packet in yaml.load(profile, Loader=get_loader()).items():
    print(name, packet.cif_7.enabled,
          [f.name for f in packet.cif_7.fields if f.enabled])
PY
```

Observed result:

```text
Query True ['max_value', 'min_value']
Reply False []
```

Also reproduced: putting `action_mode: none` before `req_s: true` raises
`ValueError: When action_mode set to none, req_s must be true`. The pinned parser
validates while reading the mapping, so YAML key order affects acceptance. Our
current query definition already uses the accepted order. Record or fix/test this
upstream behavior; do not mistake it for a protocol constraint.

The CIF7 probe establishes a parser/model limitation only. No generated C++
capability packet was compiled, transmitted or independently decoded in this
analysis. No full verification suites or privileged tests were run for this
documentation-only work.

## Next P1 work

1. Extend the implemented selector/acknowledgment tests beyond the rejected empty
   and value-bearing queries to other unsupported combinations and context fields.
2. Extend state-error, interrupted-command and unchanged-state coverage beyond the
   implemented range, precision and late-start cases.
3. Resolve capability semantics and reproduce the full generated query/response
   path. If CIF7 is selected, add a narrowly scoped pinned-source correction and
   tests for inheritance, generation, lengths and independent decoding.
4. Continue timing, replay, stalled-client and resource-bound tests independently.
   These do not depend on resolving capability semantics or on privileged access.

P1.1 analysis is complete. P1 remains open; the command examples reduce uncertainty
but do not close the capability-query requirement or establish standards compliance.
