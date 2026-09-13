# N12: sdr-shared-sink

**DESIGN ONLY — this directory contains an invalid proposed input, not runtime configuration.**

[Input](graphx.yml), [expected diagnostic](diagnostic.json), [catalog](../../catalog/README.md).
Target: `native-linux`. Expected phase: `compile`. Expected primary error:
`E_CARDINALITY` at `nodes.sink-west.ports.results`: Two result producers exceed sdr.result-sink maximum 1.

Status: `static-design`. Input parseability and diagnostic shape/reference coverage
are checked; a production v3 compiler has not emitted this diagnostic. No runtime
artifacts may be published on failure. Other independent diagnostics may accompany
this primary error; validation order must not hide it.

`planned-portable`: assert this exact code and path using the authoritative C++
compiler on native Linux and macOS. Target capability errors are compile-time
and do not require privileged execution. Add adjacent valid-boundary cases and
prove no partial output is published. See [verification](../../verification.md).
