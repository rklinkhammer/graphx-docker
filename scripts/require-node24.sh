#!/usr/bin/env bash
set -euo pipefail

command -v node >/dev/null || {
  echo "GraphX requires Node.js 24.x; node was not found" >&2
  exit 2
}
version=$(node -p 'process.versions.node')
major=${version%%.*}
test "$major" = 24 || {
  echo "GraphX requires Node.js 24.x; found Node.js $version" >&2
  exit 2
}
