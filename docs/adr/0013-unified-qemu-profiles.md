# ADR 0013: Unified external and containerized QEMU profiles

## Status

Accepted for Phase 12 implementation.

## Context

The original QEMU prototype ran one host peer and described ordinary TCP/UDP
flows using GraphX framing fields. It could be demonstrated on macOS, but it did
not provide live GUI observation or an equivalent managed Linux deployment.

## Decision

Maintain one guest, endpoint contract, passive observer, telemetry API, and GUI
with two thin deployments:

- host-managed external QEMU for portable use;
- Docker-managed QEMU for native Linux, with KVM and explicit TCG modes.

Raw edges are explicitly external data-plane edges with no GraphX framing.
They are validated and visualized but rejected by `TransportFactory`. Packet
events and packet history remain distinct from GraphX message telemetry and
history. The only control in scope pauses/resumes the origin generator.

QEMU user networking is the common default. The container profile supplies a
TCP/UDP relay at the slirp gateway so the guest image remains unchanged.

## Consequences

- Deployment behavior can differ without forking application code.
- macOS cannot prove the Linux container/KVM acceptance gate.
- The source capture is bounded by stopping QEMU when its byte ceiling is
  reached; future rotation through QMP may improve long-running operation.
- TAP, Layer 2 multicast/broadcast, physical nodes, and guest control remain
  later work.
