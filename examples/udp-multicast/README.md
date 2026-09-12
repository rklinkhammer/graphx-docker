# UDP multicast

Platform: native Linux and macOS; no Docker or privileged networking required.

This example sends five bounded GraphX envelopes to a local IPv4 multicast group and
verifies that two subscribers receive it. Run `examples/udp-multicast/run.sh` from the repository root after building GraphX.
Success prints `PASS received=5` for each subscriber.
