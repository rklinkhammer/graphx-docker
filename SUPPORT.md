# Support policy

GraphX is currently a pre-1.0 educational project. Community support is provided
through GitHub issues; there is no commercial support or uptime commitment.
Security reports follow `SECURITY.md`.

## Tested platforms

| Surface | Release-tested target | Status |
|---|---|---|
| Native Linux | Ubuntu 24.04, x86_64, C++20 and C++23 | Tier 1 |
| Native macOS | macOS 15, Apple Silicon, C++20 and C++23 | Tier 1 |
| OCI runtime images | Linux amd64 and arm64 | Tier 1 when published |
| Native Linux network labs | Docker Engine with macvlan/ipvlan, OVS, netns, nftables, netem | Capability-gated |
| Docker Desktop network simulation | Current supported Docker Desktop for macOS | Best effort |
| Windows native | Not tested | Unsupported |

Tier 1 means the release workflow must pass on the named target. It does not
mean every older patch level or downstream distribution is supported. Native
packages are host/architecture specific and do not promise a stable C++ ABI.
Node.js 22 is the telemetry production runtime; Node.js 24 runs JavaScript CI.
Python 3.10+, CMake 3.25+, Ninja, OpenSSL 3, and TShark/Wireshark are release
tooling requirements.

The newest tagged pre-1.0 release is the default supported line. Report bugs with
the exact `graphx --version`, platform, compiler/runtime versions, configuration
with secrets removed, and the smallest reproduction. Native networking reports
must also state whether the host has the required Linux capabilities.
