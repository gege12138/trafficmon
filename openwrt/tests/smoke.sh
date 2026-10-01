#!/bin/sh
set -eu

package=${1:?usage: smoke.sh /tmp/trafficmon.apk}
apk --no-network --allow-untrusted add "$package"
trafficmon --version
trafficctl --version
/etc/init.d/trafficmon enable
/etc/init.d/trafficmon start
sleep 2
trafficctl status
pidof trafficmon >/dev/null
test -S /var/run/trafficmon/trafficmon.sock
/etc/init.d/trafficmon stop
! pidof trafficmon >/dev/null
test ! -e /var/run/trafficmon/trafficmon.sock
