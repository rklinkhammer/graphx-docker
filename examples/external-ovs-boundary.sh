#!/usr/bin/env bash

graphx_external_namespace_create() {
  local token=$1 namespace=$2 host_if=$3 peer_if=$4 bridge=$5 address=$6 mac=$7
  shift 7
  local marker="graphx-external:$token:$namespace"
  ! ip link show "$host_if" >/dev/null 2>&1 || return 2
  ! ip netns list | awk '{print $1}' | grep -qx "$namespace" || return 2
  ip netns add "$namespace" || return
  ip netns exec "$namespace" ip link set lo alias "$marker" || return
  ip netns exec "$namespace" ip link set lo up || return
  ip link add "$host_if" type veth peer name "$peer_if" || return
  ip link set "$host_if" alias "$marker" || return
  ip link set "$peer_if" alias "$marker" || return
  ovs-vsctl add-port "$bridge" "$host_if" -- set Interface "$host_if" \
    external_ids:graphx_external_owner="$token" || return
  ip link set "$host_if" up || return
  ip link set "$peer_if" netns "$namespace" || return
  ip netns exec "$namespace" ip link set "$peer_if" address "$mac" || return
  ip netns exec "$namespace" ip address replace "$address" dev "$peer_if" || return
  ip netns exec "$namespace" ip link set "$peer_if" up || return
  while test "$#" -gt 0; do
    test "$#" -ge 2 || return 64
    ip netns exec "$namespace" ip route replace "$1" via "$2" dev "$peer_if" || return
    shift 2
  done
}

graphx_external_namespace_delete() {
  local token=$1 namespace=$2 host_if=$3 bridge=$4 marker="graphx-external:$1:$2"
  if ip link show "$host_if" >/dev/null 2>&1; then
    ip -d link show "$host_if" | grep -Fq "alias $marker" || {
      echo "Refusing replaced external endpoint: $host_if" >&2; return 2;
    }
    test "$(ovs-vsctl --if-exists get Interface "$host_if" external_ids:graphx_external_owner | tr -d '\"')" = "$token" || {
      echo "Refusing replaced external OVS interface: $host_if" >&2; return 2;
    }
    ovs-vsctl --if-exists del-port "$bridge" "$host_if"
    ip link delete "$host_if"
  fi
  if ip netns list | awk '{print $1}' | grep -qx "$namespace"; then
    ip netns exec "$namespace" ip -d link show lo | grep -Fq "alias $marker" || {
      echo "Refusing replaced external namespace: $namespace" >&2; return 2;
    }
    ip netns delete "$namespace"
  fi
}
