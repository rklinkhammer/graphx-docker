#!/usr/bin/env bash
set -euo pipefail

command -v node >/dev/null || {
  echo "GraphX requires Node.js 26.x; node was not found" >&2
  exit 2
}
version=$(node -p 'process.versions.node')
major=${version%%.*}
test "$major" = 26 || {
  echo "GraphX requires Node.js 26.x; found Node.js $version" >&2
  exit 2
}
