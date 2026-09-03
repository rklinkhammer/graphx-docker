#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
CLANG_FORMAT_BIN=${CLANG_FORMAT:-clang-format-18}
REQUIRED_MAJOR=18
FILES=()

command -v "$CLANG_FORMAT_BIN" >/dev/null || {
  echo "missing prerequisite: $CLANG_FORMAT_BIN" >&2
  exit 2
}

VERSION=$($CLANG_FORMAT_BIN --version)
case "$VERSION" in
  *"version $REQUIRED_MAJOR."*) ;;
  *)
    echo "GraphX formatting requires clang-format $REQUIRED_MAJOR.x; found: $VERSION" >&2
    exit 2
    ;;
esac

while IFS= read -r -d '' file; do FILES+=("$ROOT/$file"); done < <(
  git -C "$ROOT" ls-files --cached --others --exclude-standard -z -- '*.cpp' '*.hpp'
)

test "${#FILES[@]}" -gt 0 || {
  echo "no tracked C++ files found" >&2
  exit 2
}

"$CLANG_FORMAT_BIN" --dry-run --Werror "${FILES[@]}"
echo "format check passed for ${#FILES[@]} repository C++ files"
