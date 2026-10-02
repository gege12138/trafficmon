#!/bin/sh

set -eu

init_script="$1"
work="$2/run-$$"
cron_file="$work/root"
cron_line='17 3 * * * /usr/sbin/trafficarchive-uci'

mkdir -p "$work"
TM_CRON_FILE="$cron_file" TM_CRON_SERVICE=: sh -c '
    . "$1"
    start
    start
' sh "$init_script"

[ "$(grep -Fxc "$cron_line" "$cron_file")" -eq 1 ]

TM_CRON_FILE="$cron_file" TM_CRON_SERVICE=: sh -c '
    . "$1"
    stop
' sh "$init_script"

if grep -Fqx "$cron_line" "$cron_file"; then
	echo 'cron entry was not removed' >&2
	exit 1
fi

echo 'archive cron tests passed'
