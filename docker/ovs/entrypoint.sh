#!/usr/bin/env bash
set -euo pipefail
mkdir -p /run/openvswitch /var/lib/openvswitch
if [[ ! -f /var/lib/openvswitch/conf.db ]]; then
  ovsdb-tool create /var/lib/openvswitch/conf.db /usr/share/openvswitch/vswitch.ovsschema
fi
ovsdb-server --remote=punix:/run/openvswitch/db.sock --remote=db:Open_vSwitch,Open_vSwitch,manager_options \
  --pidfile --detach --log-file
ovs-vsctl --no-wait init
ovs-vswitchd --pidfile --detach --log-file

mac_if="$(ip -o -4 addr show | awk '$4 ~ /^10\.10\.0\./ {print $2; exit}')"
ipv_if="$(ip -o -4 addr show | awk '$4 ~ /^10\.20\.0\./ {print $2; exit}')"
if [[ -z "$mac_if" || -z "$ipv_if" ]]; then
  echo "could not identify both Docker network interfaces" >&2
  exit 1
fi
ip addr flush dev "$mac_if"
ip addr flush dev "$ipv_if"
ovs-vsctl --may-exist add-br br-gx-mac -- set Bridge br-gx-mac datapath_type=netdev
ovs-vsctl --may-exist add-br br-gx-ipv -- set Bridge br-gx-ipv datapath_type=netdev
ovs-vsctl --may-exist add-port br-gx-mac "$mac_if"
ovs-vsctl --may-exist add-port br-gx-ipv "$ipv_if"
ovs-vsctl --may-exist add-port br-gx-mac mirror-mac -- set Interface mirror-mac type=internal
ovs-vsctl --may-exist add-port br-gx-ipv mirror-ipv -- set Interface mirror-ipv type=internal
ip addr add 10.10.0.1/24 dev br-gx-mac
ip addr add 10.20.0.1/24 dev br-gx-ipv
ip link set br-gx-mac up
ip link set br-gx-ipv up
ip link set mirror-mac up
ip link set mirror-ipv up
ovs-vsctl -- --id=@out get Port mirror-mac -- --id=@m create Mirror name=span-mac select_all=true output-port=@out -- set Bridge br-gx-mac mirrors=@m
ovs-vsctl -- --id=@out get Port mirror-ipv -- --id=@m create Mirror name=span-ipv select_all=true output-port=@out -- set Bridge br-gx-ipv mirrors=@m
sysctl -w net.ipv4.ip_forward=1
nft -f - <<'NFT'
table inet graphx {
  chain forward {
    type filter hook forward priority 0; policy accept;
    ip saddr 10.10.0.0/24 ip daddr 10.20.0.0/24 counter accept comment "mac-sim-to-ipv-sim"
    ip saddr 10.20.0.0/24 ip daddr 10.10.0.0/24 counter accept comment "ipv-sim-to-mac-sim"
  }
}
NFT
exec tail -f /var/log/openvswitch/ovs-vswitchd.log
