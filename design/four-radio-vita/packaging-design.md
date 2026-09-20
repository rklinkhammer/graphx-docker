# P5 packaging and authored example

P5 uses the existing shared `vita` image role, catalog derivation, credential
staging and `graphx example`/`graphx run` lifecycle. It adds no launcher, source
Compose overlay or configuration format.

VITA type definitions and wire schemas join the authoritative source catalog.
VITA binaries remain an opt-in CMake dependency. Image releases select the VITA
role explicitly with `--with-vita`; derived catalogs exclude unavailable optional
roles. The example workflow selects that role from its normalized node types.
Source catalog images are templates; only the derived catalog's verified OCI
pins authorize actual execution.

Normal VITA images have qualification hooks disabled. Private P3/P4 builds opt
in separately with `--qualification-hooks`. Build metadata records this choice;
image verification checks it against the release manifest. Qualification-only
executables are absent from normal images. Existing P3/P4 preparation requires
hook-enabled candidates and must continue to reject normal images.

The maintained seven-application graph uses four independent radio identities,
four distinct TCP control ports and four UDP data ports, source-bound OVS
connections, a 9000-byte MTU and a passive recorder. The processor controls all
radios over staged mTLS credentials. The graph selects available startup and
retains authenticated management connectivity. Diagnostic capture is explicit
and independent of the receive/discard recorder. No scenario extension is needed
for baseline plan/up/status/logs/down verification.

Validation covers the authored graph and compiler output, credential references,
OVS-only application paths, optional-role selection, immutable image inventories,
license/SBOM provenance and rejection of qualification metadata substitution.
The P1 harness exercises exact packaged radio bytes in an unprivileged Linux
container. Actual complete-graph execution uses the normal CLI in the dedicated
Lima guest after reviewing the concrete artifacts and authorizing P5 privileged
verification. P3/P4 authorization alone does not cover that new run. P6 sustained
acceptance, browser qualification and image publication are outside this phase.
