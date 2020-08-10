#!/bin/bash

NS1="ns1"
NS2="ns2"
VETH1="v1"
VETH2="v2"
VETH1_PEER="v1_r"
VETH2_PEER="v2_r"
VETH1_IP="10.10.10.2"
VETH2_IP="10.10.20.2"
VETH1_PEER_IP="10.10.10.1"
VETH2_PEER_IP="10.10.20.1"
NETMASK="24"
DST1="10.10.10.0"
DST2="10.10.20.0"
TUN1_IP="10.10.100.10"
TUN2_IP="10.10.200.10"

function suppress_error
{
	exec 3<&2
	exec 2<&-
}

function enable_error
{
	exec 2<&3
	exec 3<&-
}

function enable_forward
{
	echo  "net.ipv4.ip_forward=1
		   net.ipv6.conf.default.forwarding=1
	       net.ipv6.conf.all.forwarding=1" > /etc/sysctl.d/30-ipforward.conf
}

function setup_ns
{
	local NS=$(eval echo "\$NS$1")
	local VETH=$(eval echo "\$VETH$1")
	local VETH_IP=$(eval echo "\$VETH$1_IP")
	local VETH_PEER=$(eval echo "\$VETH$1_PEER")
	local VETH_PEER_IP=$(eval echo "\$VETH$1_PEER_IP")
	local DST=$(eval echo "\$DST$2")

	ip netns add "$NS"
	ip link  add "$VETH" type veth peer name "$VETH_PEER"
	ip link  set "$VETH" netns "$NS"
	ip netns exec "$NS" ifconfig "$VETH" "$VETH_IP/$NETMASK"
	ifconfig "$VETH_PEER" "$VETH_PEER_IP/$NETMASK"
	ip netns exec "$NS" ip route add "$DST/$NETMASK" via "$VETH_PEER_IP"
}

function setup_ipip
{
	local NS=$(eval echo "\$NS$1")
	local VETH=$(eval echo "\$VETH$1")
	local LOCAL=$(eval echo "\$VETH$1_IP")
	local REMOTE=$(eval echo "\$VETH$2_IP")
	local VETH_PEER_IP=$(eval echo "\$VETH$1_PEER_IP")
	local TUN_IP=$(eval echo "\$TUN$1_IP")
	local TUN_PEER_IP=$(eval echo "\$TUN$2_IP")

	ip netns exec "$NS" ip tunnel add "tun$1" mode ipip remote "$REMOTE" local "$LOCAL"
	ip netns exec "$NS" ip link set "tun$1" up
	ip netns exec "$NS" ip addr add "$TUN_IP" peer "$TUN_PEER_IP" dev "tun$1"
}

set -xeo pipefail

if [ "$1" == "start" ]; then
	enable_forward
	setup_ns 1 2
	setup_ns 2 1
	setup_ipip 1 2
	setup_ipip 2 1

elif [ "$1" == "clean" ]; then
	suppress_error
	ip netns del "$NS1" 
	ip netns del "$NS2"
	ip veth del "$VETH1_PEER"
	ip veth del "$VETH2_PEER"
	enable_error
fi

# test
# veth: ns1 ping ns2 
# ip netns exec ns1 ping 10.10.20.2
# tun: ns1 ping ns2
# ip netns exec ns1 ping 10.10.200.10

