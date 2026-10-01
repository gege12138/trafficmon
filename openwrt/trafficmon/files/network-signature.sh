#!/bin/sh

trafficmon_emit_network_signature()
{
	local subnet interface identity
	{
		for subnet in $TM_LOCAL_SUBNETS; do
			printf 'subnet:%s\n' "$subnet"
		done
		for interface in $TM_LOCAL_INTERFACES; do
			printf 'interface:%s\n' "$interface"
		done
		for identity in $TM_LOCAL_INTERFACE_IDENTITIES; do
			printf 'ifindex:%s\n' "$identity"
		done
	} | LC_ALL=C sort -u
}
