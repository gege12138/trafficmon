#!/bin/sh
set -eu

client_ns=${CLIENT_NS:-tm3b_client}
router=${ROUTER:-192.168.50.1}
wan4_url=${WAN4_URL:-http://198.51.100.2/}
wan6_url=${WAN6_URL:-http://[fd51::2]/}
client4=${CLIENT4:-192.168.50.2}
client6_prefix=${CLIENT6_PREFIX:-fd52:}

ip netns exec "$client_ns" curl -4 --fail --silent --show-error --output /dev/null "$wan4_url"
ip netns exec "$client_ns" curl -6 --fail --silent --show-error --output /dev/null "$wan6_url"
sleep "${REFRESH_WAIT:-35}"
json="$(ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@"$router" trafficctl json)"
python3 -c '
import json, sys
rows = json.loads(sys.stdin.read())["records"]
v4, v6_prefix = sys.argv[1:]
def valid(row):
    return (row["connections"] > 0 and row["download_bytes"] > 0 and
            row["upload_bytes"] > 0)
assert any(r["ip"] == v4 and valid(r) for r in rows)
assert any(r["ip"].startswith(v6_prefix) and valid(r) for r in rows)
' "$client4" "$client6_prefix" <<EOF
$json
EOF
echo PASS
