#!/usr/bin/env bash
# Shared runtime policy for GraphX demos. Callers retain their small, public
# entry points; this file owns host selection and lifecycle sequencing.

graphx_demo_platform() {
  uname -s
}

graphx_demo_require() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Missing prerequisite: $1" >&2
    return 2
  }
}

graphx_demo_port() {
  local name=$1 value=$2
  case "$value" in
    ''|*[!0-9]*) echo "$name must be from 1 through 65535" >&2; return 2 ;;
  esac
  if test "$value" -lt 1 || test "$value" -gt 65535; then
    echo "$name must be from 1 through 65535" >&2
    return 2
  fi
  printf '%s\n' "$value"
}

graphx_demo_require_compose_runtime() {
  graphx_demo_require docker || return
  docker compose version >/dev/null 2>&1 || {
    echo "Docker Compose v2 is required." >&2
    return 2
  }
  if [[ $(graphx_demo_platform) == Darwin ]]; then
    local context
    context=$(docker context show 2>/dev/null || true)
    if [[ $context != orbstack ]]; then
      echo "GraphX Compose demos use OrbStack on macOS; current Docker context is '${context:-unavailable}'." >&2
      echo "Start OrbStack and run: docker context use orbstack" >&2
      return 2
    fi
    docker info >/dev/null 2>&1 || {
      echo "OrbStack is selected but its Docker engine is not available." >&2
      return 2
    }
  fi
}

graphx_demo_ensure_dev_build() {
  local repo_root=$1 graphx=${GRAPHX_BIN:-$1/build/dev/graphx}
  if [[ ! -x $graphx ]]; then
    echo "GraphX Linux CLI not found at $graphx." >&2
    echo "Build the dev preset on this Linux system or set GRAPHX_BIN to its absolute path." >&2
    return 2
  fi
  [[ -x $graphx ]] || {
    echo "GraphX CLI was not produced at $graphx." >&2
    return 1
  }
  printf '%s\n' "$graphx"
}

graphx_demo_dispatch_lima() {
  local repo_root=$1 lab=$2 action=$3
  local lima_dir="$repo_root/infrastructure/lima"
  # shellcheck source=../../infrastructure/lima/common.sh
  source "$lima_dir/common.sh"
  graphx_m1_require_host
  local digest status guest_graphx deadline
  digest=$(graphx_m1_digest)
  status=$(graphx_m1_assert_identity "$digest")
  if [[ $status != Running ]]; then
    echo "GraphX Lima instance '$GRAPHX_M1_INSTANCE' is $status; run infrastructure/lima/start.sh." >&2
    return 2
  fi
  guest_graphx=${GRAPHX_LIMA_GRAPHX_BIN:-/var/lib/graphx/m1/build/dev/graphx}
  "${GRAPHX_M1_RUNNER}" 120 limactl shell --workdir "${GRAPHX_M1_GUEST_ROOT}" \
    "${GRAPHX_M1_INSTANCE}" -- bash -c \
    'test -x "$1" && docker info >/dev/null && systemctl is-active --quiet openvswitch-switch.service' \
    _ "${guest_graphx}" || {
      echo "The Lima network-lab prerequisites or Linux GraphX CLI are unavailable." >&2
      echo "Run infrastructure/lima/start.sh and infrastructure/lima/verify.sh first." >&2
      return 2
    }
  case "$action" in plan|status) deadline=120 ;; up|down) deadline=1800 ;; esac
  "${GRAPHX_M1_RUNNER}" "${deadline}" limactl shell --workdir "${GRAPHX_M1_GUEST_ROOT}" \
    "${GRAPHX_M1_INSTANCE}" -- env GRAPHX_BIN="${guest_graphx}" \
    scripts/network-lab.sh "${lab}" "${action}"
}

graphx_demo_ovs_lab_local() {
  local action=$1 example_dir=$2 project=$3 repo_root=$4 graphx=$5
  local compose_path="$repo_root/examples/network-lab.compose.yaml"
  local config="$example_dir/graphx.yaml"
  local -a compose=(sudo env GRAPHX_REPO_ROOT="$repo_root" GRAPHX_LAB_CONFIG="$config"
    docker compose -p "$project" -f "$compose_path")
  case "$action" in
    up)
      "${compose[@]}" up -d --build
      if ! sudo "$graphx" infra create "$config"; then
        "${compose[@]}" down
        return 1
      fi
      sudo "$graphx" infra status "$config"
      ;;
    status)
      sudo "$graphx" infra status "$config"
      "${compose[@]}" ps
      ;;
    down)
      sudo "$graphx" infra destroy "$config"
      "${compose[@]}" down
      ;;
    *)
      echo "unsupported action: $action" >&2
      return 64
      ;;
  esac
}

graphx_demo_ovs_lab() {
  local action=$1 example_dir=$2 project=$3 repo_root=$4
  case $(graphx_demo_platform) in
    Darwin)
      if [[ ${GRAPHX_DEMO_GUEST:-0} != 1 ]]; then
        graphx_demo_dispatch_lima "$repo_root" "$(basename "$example_dir")" "$action"
        return
      fi
      ;;
    Linux) ;;
    *)
      echo "Privileged GraphX network labs require native Linux or macOS with Lima." >&2
      return 1
      ;;
  esac
  graphx_demo_require_compose_runtime
  local graphx
  graphx=$(graphx_demo_ensure_dev_build "$repo_root") || return
  graphx_demo_ovs_lab_local "$action" "$example_dir" "$project" "$repo_root" "$graphx"
}

graphx_demo_network_lab() {
  local repo_root=$1 lab=$2 action=$3
  case "$lab" in macvlan|ipvlan-l2|ipvlan-l3|mixed-network) ;;
    *) echo "unsupported network laboratory: $lab" >&2; return 64 ;;
  esac
  case "$action" in plan|up|status|down) ;;
    *) echo "unsupported network laboratory action: $action" >&2; return 64 ;;
  esac
  case $(graphx_demo_platform) in
    Linux)
      local graphx
      graphx=$(graphx_demo_ensure_dev_build "$repo_root") || return
      if [[ $action == plan ]]; then
        "$graphx" infra create "$repo_root/examples/$lab/graphx.yaml" --dry-run
      else
        "$repo_root/examples/$lab/scripts/$action.sh"
      fi
      ;;
    Darwin) graphx_demo_dispatch_lima "$repo_root" "$lab" "$action" ;;
    *)
      echo "system-OVS network laboratories require Linux or Apple Silicon macOS with GraphX Lima." >&2
      return 2
      ;;
  esac
}
