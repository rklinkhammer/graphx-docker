# Proposed reusable catalog

**DESIGN ONLY.** Everything in this directory is a non-runtime contract example.

`lock.json` pins each catalog input by SHA-256. Its digest is SHA-256 of the exact
lock file bytes and appears in every expected resolved graph. Type definitions
are shared by every fixture; graphs cannot override images, commands or ports.
`types/*.json` each define one reusable type, `templates.json` defines the two
bounded rendering templates, `platform.json` supplies platform defaults,
`wire-schemas.json` supplies wire contracts and `targets.json` declares the four
targets. `guests/*.json` are separate guest build contracts. `sources.json` resolves their
source references to declared release input trees and explicitly records missing
implementation work. Guest recipes and
future image hashes are illustrative until the implementation release gate.

`graph.schema.json` is a closed structural draft-2020-12 schema for authored v3.
`type.schema.json` is a closed type schema. `normalized.schema.json` currently
checks the normalized outer shape; deeper normalized checks are performed by
static fixture comparisons and specified normatively in architecture.md.
It is deliberately not advertised as the complete production normalized schema.
The implementation must produce the complete nested schema alongside consumers.

All semantic checks, defaults and expansion belong to C++. JSON Schema does not
assign endpoints, test CIDRs, prove reachability, check SCC feedback or allocate
names. Parameter values are checked against the referenced type, not an arbitrary
free-form application object. A new type may add a fixed executable and typed
parameter schema, but cannot embed shell, Compose overrides, conditionals,
script hooks, dependency inference or a second configuration parser.

The source release contains GraphX C++ binaries, native Node.js 26 platform plus
web assets, SDR wrappers and diagnostic tools. Their lock covers executable
bytes and dependencies per architecture. The shared runtime container drops the
current baked-in graph and default generator entrypoint. Topology changes do not
build containers. New software uses a catalog-owned reviewed build recipe with
source digest, builder image digest, literal argv, bounded resource limits,
network-disabled build after dependency fetch, fixed SOURCE_DATE_EPOCH, output
checksums and SBOM. User graph nodes only select types.

The design catalog's guest pins remain illustrative review inputs. The common
implementation under `guests/` builds echo and SDR radio applications with the
same configuration/credential/readiness agent. Use the generated guest release
catalog for execution; its source digest, builder pin and output checksums come
from verified build artifacts. Neither selecting `guest.echo` nor attaching a
TAP supplies a radio application. Build failure produces no eligible guest set.
