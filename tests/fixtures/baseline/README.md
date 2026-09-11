# Simplification behavior baseline

These fixtures capture public CLI behavior before the internal simplification
refactor. `test_simplification_baseline.py` compares successful validation,
inspection, projection, migration, infrastructure dry-run planning, manual
route planning, strict malformed-input diagnostics, and version-1 mutation
refusal against these files.

Temporary state paths are represented as `<STATE_ROOT>` and repository paths as
`<SOURCE_ROOT>`. Other content is deliberately exact. Update a fixture only
after reviewing and accepting the corresponding externally visible behavior
change.
