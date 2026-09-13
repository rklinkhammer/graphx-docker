# Catalog release identities

The checked-in catalog is the development compiler input. Its illustrative image
and guest identities remain explicitly unverified. Do not use it to deploy a graph.

`scripts/release/image_release.py build` produces a release-specific copy of this
same catalog from verified shared OCI archives. It changes container type image
pins and revisions together, replaces the platform image pin, preserves the two
fixed template contracts, and repins the complete lock. `release-images.json` in
that generated catalog records the image architecture, OCI manifest/config
digests and archive checksums. The native package has its own independently
verified manifest and SPDX inventory. Guest identities remain the P8 gate.

The generated image references identify offline OCI artifacts. They are not a
claim that an image has been published to a registry or is pullable by Docker.
Publication must preserve the verified OCI manifest or derive and verify new
registry pins. A Docker image ID is not an OCI manifest digest. Compilation alone
does not verify image bytes or enable execution.

See [release process](../../docs/release-process.md) for commands and verification.
