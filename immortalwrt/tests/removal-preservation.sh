#!/bin/sh
set -eu

db_dir="$(uci -q get trafficmon.main.database_directory)"
protocols="$(uci -q get trafficmon.main.protocol_database)"
before=/tmp/trafficmon-remove.before
after=/tmp/trafficmon-remove.after

trafficctl commit
sha256sum "$protocols" "$db_dir/current.db" >"$before"
opkg remove trafficmon
test -f "$protocols"
test -f "$db_dir/current.db"
sha256sum "$protocols" "$db_dir/current.db" >"$after"
cmp "$before" "$after"
echo PASS
