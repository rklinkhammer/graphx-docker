# Contributing

Use a focused branch and keep protocol, configuration, runtime, tests, and docs
consistent. Do not commit credentials, private certificates, capture payloads,
generated release assets, or local build output.

Before requesting review, run the portable checks in `docs/test-procedure.md` and
the checks relevant to the files changed. Changes to the envelope, configuration,
control, capture, or persistence contracts require compatibility analysis and an
ADR when consequential. Breaking changes require an explicit version/migration
plan under `docs/compatibility-policy.md`.

Maintainers alone create release tags. A contribution must not publish images or
assets. Update `CHANGELOG.md` under **Unreleased** for user-visible behavior.
Security findings follow `SECURITY.md`, not the public issue tracker.
