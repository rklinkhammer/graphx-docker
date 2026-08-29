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
