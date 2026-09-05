#!/usr/bin/env bash
set -euo pipefail
profile_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
exec "$profile_dir/scripts/demo-profile.sh" container "$@"
