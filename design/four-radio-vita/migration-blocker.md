# Scheduled sample epoch regression

Status: **resolved locally** with `vrt_framework`
`dbe85d37155145842da60367af1c4beef8801b0c`.
The unchanged reproducer passed both modes before application integration resumed.
Application evidence is recorded separately in [P1 verification](p1-verification.md).

## Contract

The common scheduled UTC start establishes the simulated sample epoch. Backend
`actual_time` and terminal AckX report actual device execution time. AckV reports
admission/scheduling; AckS reports observation time. The library supplies
`EffectiveEvent::sample_epoch` to the GraphX source adapter and owns Context
association, packet timestamps and bounded pacing. GraphX neither substitutes
scheduled time for actual execution nor rewrites packet timestamps.

The same Soapy device resets its local sample ordinal and deterministic initial
phase on the successful epoch event. Later packet requests advance that ordinal,
including skipped samples, without resetting phase at packet/burst boundaries.
The source's epoch reset does not occur on an idempotent command retry.

## Reproducer and results

On native macOS arm64 with Apple clang 21.0.0 (`clang-2100.3.34.2`):

```sh
git -C /Users/rklinkhammer/workspace/vrt_framework rev-parse HEAD
mkdir -p outputs/vita-migration
c++ -std=c++23 -O0 -fno-rtti \
  -I /Users/rklinkhammer/workspace/vrt_framework/include \
  tests/repro_vita_start_epoch.cpp -o outputs/vita-migration/start-epoch
outputs/vita-migration/start-epoch --on-time
outputs/vita-migration/start-epoch
```

Both exit 0, with the original timestamp assertion preserved:

```text
scheduled=1000:50000000000 actual=1000:50000000000 first_sample=1000:50000000000
scheduled=1000:50000000000 actual=1000:50500000000 first_sample=1000:50000000000
```

The opt-in build registers the same source as `graphx-vita-start-on-time` and
`graphx-vita-start-epoch`. The assertion remains a regression gate. This library
reproducer alone does not establish Soapy, mTLS, multi-process or OVS acceptance.
See the independent four-radio common-start test and the evidence matrix in
[P1 verification](p1-verification.md) for the application boundary.
