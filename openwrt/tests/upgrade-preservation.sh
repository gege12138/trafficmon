#!/bin/sh
set -eu

package=${1:?usage: upgrade-preservation.sh /tmp/new-trafficmon.apk}
db_dir="$(uci -q get trafficmon.main.database_directory)"
protocols="$(uci -q get trafficmon.main.protocol_database)"
before=/tmp/trafficmon-upgrade.before
after=/tmp/trafficmon-upgrade.after

trafficctl commit
/etc/init.d/trafficmon stop
sha256sum /etc/config/trafficmon "$protocols" "$db_dir/current.db" >"$before"
apk --no-network --allow-untrusted add --upgrade "$package"
sha256sum /etc/config/trafficmon "$protocols" "$db_dir/current.db" >"$after"
cmp "$before" "$after"
/etc/init.d/trafficmon start
sleep 2
trafficctl status | grep '"collector_enabled":true'
trafficctl json >/dev/null
echo PASS
