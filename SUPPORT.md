# Support

GraphX 1.1.0 supports:

| Area | Supported environment |
|---|---|
| Portable build and tests | Linux and macOS; CMake 3.25+, Ninja, OpenSSL 3, C++23 |
| Compose demos | Docker Engine on Linux; OrbStack on macOS |
| Privileged OVS networking | Native Linux or the GraphX ARM64 Lima guest |
| QEMU TAP lab | Linux/Lima; x86_64 guest with KVM when available or TCG |
| Browser UI | Current Chromium, Firefox, and Safari |

Docker Desktop is not a supported macOS runtime. Privileged Linux features are not
reported as native macOS evidence. For usage questions, open a repository issue with
the GraphX version, host platform, runtime, command, and sanitized output. Report
security issues through [`SECURITY.md`](SECURITY.md).
