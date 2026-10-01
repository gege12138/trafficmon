#!/bin/sh
set -eu

mode=${1:?usage: reboot-persistence.sh before|after}
db_dir="$(uci -q get trafficmon.main.database_directory)"
state="$db_dir/reboot.expected.sha256"

case "$mode" in
    before)
        trafficctl commit
        /etc/init.d/trafficmon stop
        sha256sum "$db_dir/current.db" | awk '{ print $1 }' >"$state"
        test -s "$state"
        echo 'state saved; reboot the router, then run this script with after'
        ;;
    after)
        /etc/init.d/trafficmon enabled
        /etc/init.d/trafficmon running
        expected="$(cat "$state")"
        actual="$(sha256sum "$db_dir/current.db" | awk '{ print $1 }')"
        test "$expected" = "$actual"
        rm -f "$state"
        trafficctl json >/dev/null
        trafficctl status | grep '"collector_enabled":true'
        echo PASS
        ;;
    *)
        echo "unknown mode: $mode" >&2
        exit 2
        ;;
esac
