#!/bin/sh
set -eu

helper="$1"
hotplug="$2"
workdir="$3"
rm -rf "$workdir"
mkdir -p "$workdir"

TM_LOCAL_SUBNETS='fd00::/64 192.168.1.0/24 fd00::/64'
TM_LOCAL_INTERFACES='br-home br-lan br-home'
TM_LOCAL_INTERFACE_IDENTITIES='br-home:12 br-lan:8 br-home:12'
. "$helper"
trafficmon_emit_network_signature > "$workdir/canonical"
cat > "$workdir/expected" <<'EOF'
ifindex:br-home:12
ifindex:br-lan:8
interface:br-home
interface:br-lan
subnet:192.168.1.0/24
subnet:fd00::/64
EOF
cmp "$workdir/expected" "$workdir/canonical"

signature="$workdir/applied-signature"
current="$workdir/current-signature"
reloads="$workdir/reloads"
logs="$workdir/logs"
: > "$reloads"
: > "$logs"

cat > "$workdir/init" <<'EOF'
#!/bin/sh
case "$1" in
	prefix_signature)
		[ -f "$TRAFFICMON_CURRENT_SIGNATURE" ] || exit 1
		cat "$TRAFFICMON_CURRENT_SIGNATURE"
		;;
	reload)
		echo reload >> "$TRAFFICMON_RELOADS"
		;;
	*) exit 2 ;;
esac
EOF
cat > "$workdir/logger" <<'EOF'
#!/bin/sh
echo "$*" >> "$TRAFFICMON_LOGS"
EOF
chmod +x "$workdir/init" "$workdir/logger"

run_event()
{
	ACTION="$1" INTERFACE="$2" \
	TRAFFICMON_INIT_SCRIPT="$workdir/init" \
	TRAFFICMON_SIGNATURE_FILE="$signature" \
	TRAFFICMON_CURRENT_SIGNATURE="$current" \
	TRAFFICMON_RELOADS="$reloads" \
	TRAFFICMON_LOGGER="$workdir/logger" \
	TRAFFICMON_LOGS="$logs" \
	sh "$hotplug"
}

write_signature()
{
	printf '%s\n' "$1" > "$2"
}

# 1. Same prefix, different local device must reload.
write_signature 'interface:br-lan
ifindex:br-lan:8
subnet:192.168.1.0/24' "$signature"
write_signature 'interface:br-home
ifindex:br-home:12
subnet:192.168.1.0/24' "$current"
run_event ifupdate lan
[ "$(wc -l < "$reloads")" -eq 1 ]
cmp "$current" "$signature"

# 2. A transient prefix-less down invalidates state; same-prefix up must reload.
: > "$reloads"
rm -f "$current"
run_event ifdown lan
[ ! -e "$signature" ]
[ ! -s "$reloads" ]
write_signature 'interface:br-home
ifindex:br-home:12
subnet:192.168.1.0/24' "$current"
run_event ifup lan
[ "$(wc -l < "$reloads")" -eq 1 ]
cmp "$current" "$signature"

# 3. Down/up with a rebuilt device and unchanged prefix rebuilds tracking.
: > "$reloads"
rm -f "$current"
run_event ifdown lan
write_signature 'interface:br-home
ifindex:br-home:37
subnet:192.168.1.0/24' "$current"
run_event ifup lan
[ "$(wc -l < "$reloads")" -eq 1 ]
grep -qx 'interface:br-home' "$signature"
grep -qx 'ifindex:br-home:37' "$signature"

# 4. Unrelated events with an unchanged complete signature do not reload.
: > "$reloads"
cp "$current" "$signature"
run_event ifupdate wan
[ ! -s "$reloads" ]

echo 'PASS local network signature transitions'
