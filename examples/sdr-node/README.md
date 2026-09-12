# SDR node examples

The simulated profile runs without hardware. The external profile models a raw
device data plane, origin-owned control, mutual TLS, and OVS attachment.
The simulated profile also runs packet capture, history, and the telemetry UI. GraphX does not configure physical radio hardware; the operator owns
device setup and safety.

Use [simulated SDR](simulated/README.md) with Docker Engine on Linux or OrbStack
on macOS. Use [external SDR](external/README.md) with sudo and system OVS on
native Linux or from an explicit shell in the GraphX Lima guest. The external
example uses a simulated device in a namespace; it does not require radio hardware.
