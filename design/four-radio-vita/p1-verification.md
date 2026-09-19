# P1 standalone radio verification

Status: **in progress**, with a working opt-in radio and passing standalone tests.
This is development evidence, not a released four-radio example or VITA standards
certification. The remaining P1 work below must be completed before closing P1.

[P1.1 command analysis](command-analysis.md) is complete. It identifies status
selector and acknowledgment corrections now applied to the radio, and reproduces missing CIF7 inheritance
in the pinned dependency's query acknowledgment model. Capability semantics remain
unverified; this finding does not block independent timing/failure tests. The
capability architecture is selected: read-only VITA command/response on the existing
TCP/mTLS control channel, with streaming context reserved for applied settings.
That design decision is not evidence of a working capability endpoint.

## Implemented surface

- `include/graphx/vita/` and `src/vita/` implement the SoapySDR virtual device,
  paced radio, generated VITA integration and authenticated control service;
  `apps/vita-radio/main.cpp` is the thin entry point.
- `config/vita/`, `cmake/VitaRadio.cmake` and `scripts/vita/` provide the opt-in
  native type, packet definitions, pinned dependencies and reproducible generation.
  Dependency licenses are copied into the build. Published releases and OCI images
  do not yet contain this executable.
- `src/node_settings.cpp` now applies resolved source addresses and staged TLS
  credentials to raw external transports, as it already did for GraphX transports.
  The radio exercises that shared binding path through authoritative normalization.
- `tests/test_vita_device.cpp` and `tests/test_vita_radio.py` exercise the actual
  device and standalone executable. The Python harness encodes/decodes protocol
  bytes independently of the generated C++ codec.

See [build and test instructions](README.md) and [radio design](radio-design.md).

## Executed checks

Environment: macOS developer host, native processes and unprivileged loopback
sockets. No Docker engine, OVS, Lima, Linux guest, physical SDR or privileged
operation was used. Generator environment: Python 3.13; CTest selected Python 3.14
for the independent standard-library harness. Tests used temporary credentials.

```sh
cmake --build build/vita --target graphx-cli graphx-vita-radio-app graphx-vita-device-test
ctest --test-dir build/vita -R graphx-vita -V
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quick
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh portable
PATH=/opt/homebrew/opt/node@24/bin:$PATH scripts/verify.sh quality
/opt/homebrew/opt/llvm@21/bin/clang-tidy -p build/vita \
  --extra-arg=-isysroot \
  --extra-arg=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk \
  src/vita/virtual_device.cpp src/vita/radio.cpp \
  tests/test_vita_device.cpp apps/vita-radio/main.cpp
cppcheck --enable=warning,portability --inline-suppr \
  --suppress=missingIncludeSystem --error-exitcode=1 -Iinclude \
  src/vita/virtual_device.cpp src/vita/radio.cpp \
  tests/test_vita_device.cpp apps/vita-radio/main.cpp
```

Quick verification passes all 41 development tests. Portable verification and the
repository quality checks pass. The opt-in native radio type
is separate from the published catalog. The radio's command-analysis changes also
pass both standalone tests and explicit clang-tidy/cppcheck checks. The default
build excludes this radio, so default-suite success alone does not establish its
protocol behavior.
No shell scripts were changed; ShellCheck was not applicable.

Standalone verification passed both device and process tests. Four concurrent radio
processes used independent signal settings. The receiver decoded 512 full data
packets from radio 2 (524,288 IQ pairs, two maximum-size bursts); radios 1 and 4
each provided 20 packets containing 14,348 pairs including two-pair final packets;
radio 3 provided 20 single-packet bursts of 32 pairs. Each first packet arrived
within the tested 100 ms host-start tolerance. Exact observed delays are in the
standalone log; these are host observations, not a synchronization guarantee.

Generated logs:

- `outputs/four-radio-vita/p1/standalone.log`
- `outputs/four-radio-vita/p1/clang-tidy.log`
- `outputs/four-radio-vita/p1/cppcheck.log`
- `outputs/verification/20260916T013715Z-quick.log`
- `outputs/verification/20260916T013901Z-portable.log`
- `outputs/verification/20260916T014059Z-quality.log`

## Applied command-analysis checks

The standalone harness now independently verifies:

- execution plus status requests produce two correlated packets, with the expected
  CAM flags and exact applied setting values;
- NO_ACTION status selects only sample rate or all supported settings/state, with
  the query acknowledgment's action mode and exact packet lengths;
- empty selectors and selector queries containing execute-style values are rejected;
- RF-frequency, bandwidth and gain range errors identify their fields, nonintegral
  sample rates report unsupported precision, and rejected settings remain unchanged;
- late starts carry the discrete-I/O timestamp-error indication and timing-issues
  acknowledgment mode;
- execution-only stop does not leave an unsolicited status packet in the stream.

The query acknowledgment is generated separately from the execute acknowledgment.
Selector-only requests use the generated query decoder; they are never decoded as
execute value payloads. After this parsing correction, both focused tests and the
radio-specific clang-tidy/cppcheck checks passed again. Those final checks used the
same commands above, with `src/vita/radio.cpp` as the sole static-analysis input.
The default builds do not compile this opt-in source.
CIF7 capability-query generation remains unimplemented and unverified. Broader
state-error, context and failure tests remain listed below. These additions do not
establish normative VITA compliance or a complete P1 exit.

## Requirement coverage and remaining work

| Requirement | Implementation | Verification | Status |
|---|---|---|---|
| Independent SoapySDR devices | One subclass instance/stream per process | Device isolation and four concurrent processes | Tested |
| Signal and bounds | Fixed RF tone, tuning, passband, gain, clipping, phase, bounded CS16 reads | Device assertions and independently decoded IQ | Tested for selected cases |
| VITA data | Generated stream/class/trailer layout, full/short packets, burst markers, count wrap | Independent byte decoder and two maximum bursts | Tested |
| Simulated timestamps and pacing | Integer sample time; scheduled steady-clock start; bounded overdue-sample skipping | Sample progression and first-emission timing | Tested at 1 MSample/s; other rates/overrun injection remain |
| Authenticated control | Staged credential hashes, mTLS, controller identity, bounded framing, configuration/status/start/stop | Fragmented/coalesced requests, unauthorized identity, malformed size, invalid settings, duplicate start | Tested for listed cases |
| Controller reconnect and receiver loss | Replay history survives connections; UDP reception never blocks control | Reconnect/replay during streaming, close UDP receiver then stop/query | Tested |
| Capability query | Selected read-only VITA command/response over existing TCP/mTLS; SoapySDR range API available | Local device range assertions only; pre-configuration/stopped/streaming wire checks required | Architecture decided; field mapping and runtime query remain unimplemented |
| Status and acknowledgments | Selector-only queries; separate generated query reply; requested execution/status replies | Independent CAM, selector, size, field-value and correlation checks; combined execution/status request | Tested for selected cases |
| Setting errors | Per-field range/precision errors; late-start timestamp error | Independent EIF/reason bits and unchanged applied state after rejection | Tested for selected cases |
| Context | Generated context before first data/each burst | Data decoder does not independently validate every context field | Broader independent vectors required |
| Failure/resource bounds | Bounded input/output/replay, 2-second session deadlines, nonblocking loop, signal shutdown | Owned process termination and selected malformed inputs | Stalled-client, replay exhaustion and sustained-load coverage remain |
| Configuration | Native opt-in type through authoritative loader; raw bindings reuse credential staging | Private catalog normalization; invalid radio index rejected | Tested locally; explicit UDP source-port contract remains |
| Dependencies | Hash-pinned source archives, pinned generator packages, build-local documented template fixes, license inventory | Native build/generation | Published release/SBOM integration belongs to P5 |
| OVS/container deployment | Not implemented by P1 | Not run | Later phases |

Remaining P1 work: finish the standards-backed capability-query encoding, extend
independent context/acknowledgment vectors and failure-case tests, and validate
non-divisor sample rates and supported setting-change timing. Document these as
unfinished requirements rather than silently replacing capability discovery with
status. Credential rotation is not implemented in this server. On-wire dynamic
retuning while streaming is rejected; device-level changes are a separate tested
API surface. No new product decision is requested by this checkpoint.
