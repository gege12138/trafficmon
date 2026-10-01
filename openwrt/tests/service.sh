#!/bin/sh
set -eu

echo 'enable/start/status'
/etc/init.d/trafficmon enable
/etc/init.d/trafficmon start
sleep 2
/etc/init.d/trafficmon enabled
/etc/init.d/trafficmon running
trafficctl status | grep '"collector_enabled":true'
test -S /var/run/trafficmon/trafficmon.sock

echo 'reload'
before="$(pidof trafficmon)"
/etc/init.d/trafficmon reload
sleep 2
after="$(pidof trafficmon)"
test "$before" != "$after"
trafficctl status >/dev/null

echo 'restart'
before="$after"
/etc/init.d/trafficmon restart
sleep 2
after="$(pidof trafficmon)"
test "$before" != "$after"

echo 'abnormal crash/respawn'
kill -KILL "$after"
sleep 8
respawned="$(pidof trafficmon)"
test "$after" != "$respawned"
trafficctl status | grep '"collector_enabled":true'

echo 'normal stop/start'
/etc/init.d/trafficmon stop
sleep 1
! pidof trafficmon >/dev/null
test ! -e /var/run/trafficmon/trafficmon.sock
/etc/init.d/trafficmon start
sleep 2
trafficctl status >/dev/null
echo PASS
