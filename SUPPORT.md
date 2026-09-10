# Support policy

GraphX is a 1.0 educational project. Community support is provided
through GitHub issues; there is no commercial support or uptime commitment.
Security reports follow `SECURITY.md`.

## Tested platforms

| Surface | Release-tested target | Status |
|---|---|---|
| Native Linux | Ubuntu 24.04, x86_64, C++20 and C++23 | Tier 1 |
| Native macOS | macOS 15, Apple Silicon, C++20 and C++23 | Tier 1 |
| OCI runtime images | Linux amd64 and arm64 | Tier 1 when published |
| Native Linux network labs | Docker Engine with macvlan/ipvlan, OVS, netns, nftables, netem | Capability-gated |
| Lima M1 execution environment | Apple Silicon, Lima 2.2+, ARM64 Ubuntu 24.04 guest | Capability-gated |
| Configuration v2 model and migration | Native Linux and macOS portable CLI | M2 implemented; realization deferred |
| Docker Desktop network simulation | Current supported Docker Desktop for macOS | Best effort |
| Windows native | Not tested | Unsupported |

Tier 1 means the release workflow must pass on the named target. It does not
mean every older patch level or downstream distribution is supported. Native
packages are host/architecture specific and do not promise a stable C++ ABI.
Node.js 22 is the telemetry production runtime; Node.js 24 runs JavaScript CI.
Python 3.10+, CMake 3.25+, Ninja, OpenSSL 3, and TShark/Wireshark are release
tooling requirements.

The newest tagged 1.x release is the default supported line. Report bugs with
the exact `graphx --version`, platform, compiler/runtime versions, configuration
with secrets removed, and the smallest reproduction. Native networking reports
must also state whether the host has the required Linux capabilities.

The Lima M1 environment is the supported macOS foundation for privileged Linux
networking work. M1 verifies rootful Docker, system OVS, disposable
namespace/veth/TAP, nftables, netem, capture, and the portable baseline inside
the VM. M2 adds portable parsing, inspection, projection, and deterministic
migration for configuration version 2. M3 realizes only identity-marked OVS
bridges with persistent VM-local ownership state and recovery. It does not yet
attach GraphX containers or namespaces by veth, configure endpoint ports or
mirrors, or replace QEMU slirp with TAP.
