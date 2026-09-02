# Third-party software

## yaml-cpp

- Version: 0.9.0
- Source: <https://github.com/jbeder/yaml-cpp>
- License: MIT
- Archive SHA-256: `25cb043240f828a8c51beb830569634bc7ac603978e0f69d6b63558dadefd49a`

GraphX uses yaml-cpp to parse the authoritative YAML configuration. CMake
first accepts an installed compatible package and otherwise downloads the
versioned archive while verifying the hash above. The upstream license is
included with its source archive and build output.

## Optional operations images

The optional `compose.observability.yaml` projection uses immutable image
digests in addition to human-readable version tags:

- Prometheus 3.13.0, Apache-2.0,
  `prom/prometheus:v3.13.0@sha256:c6b27ea434f8389bfe233fbc7be381cf50587c286e871bc842008f5a1b1908a7`;
- Grafana 13.2.0, AGPL-3.0-only,
  `grafana/grafana:13.2.0@sha256:3fd54ae1214669f8355f065ec9f6445d5279a3d77095ab048ca045685272429b`.

These images are deployment tooling rather than linked GraphX libraries. Their
upstream images contain the corresponding license and notices. Operators remain
responsible for reviewing image provenance and license obligations for their
distribution model.
