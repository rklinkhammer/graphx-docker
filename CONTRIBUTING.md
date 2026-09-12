# Contributing

Keep code, tests, examples, schemas, and documentation aligned with the current
system. Record development history in Git rather than adding snapshots or superseded
fixtures to the maintained tree.

Before submitting a change:

1. Follow the invariants in [`AGENTS.md`](AGENTS.md) and
   [`docs/project-decisions.md`](docs/project-decisions.md).
2. Update the authoritative configuration model and normalized contract together.
3. Add focused tests for changed behavior and remove superseded expectations.
4. Run `scripts/verify.sh portable`; run `full` or `native-linux` when the
   affected surface requires it.
5. Update current user documentation and examples in the same change.

Do not commit credentials, build trees, captures, runtime ledgers, generated release
artifacts, or host-specific state.
