# External SDR profile

This profile uses a Linux namespace as the bounded external-device endpoint and
keeps the raw SDR edge outside the GraphX envelope transport factory. The demo
generates short-lived TLS material, realizes the OVS boundary, runs the services,
and removes only identity-owned resources during cleanup.
