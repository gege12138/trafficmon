#!/bin/sh

set -eu

manager="$1"
work="$2/run-$$"
fake="$work/fake"
journal="$work/journal"
archive="$work/archive"
state="$work/state"
export_count="$work/export.count"
upload_count="$work/upload.count"

fail()
{
	echo "FAIL: $*" >&2
	exit 1
}

count_is()
{
	actual=0
	[ ! -f "$1" ] || actual="$(cat "$1")"
	[ "$actual" = "$2" ] || fail "$1 expected $2, got $actual"
}

mkdir -p "$fake" "$journal" "$archive" "$state"

cat > "$fake/trafficexport" <<'EOF'
#!/bin/sh
count=0
[ ! -f "$TM_EXPORT_COUNT" ] || count="$(cat "$TM_EXPORT_COUNT")"
count=$((count + 1))
printf '%s\n' "$count" > "$TM_EXPORT_COUNT"
[ "${TM_FAIL_EXPORT:-0}" != 1 ] || exit 1
output=''
while [ "$#" -gt 0 ]; do
	if [ "$1" = '--output' ]; then output="$2"; shift 2; else shift; fi
done
[ -n "$output" ] || exit 2
printf 'timestamp,device_id,src_ip,dst_ip,src_port,dst_port,protocol,rx_bytes,tx_bytes\r\n' > "$output"
EOF

cat > "$fake/trafficupload" <<'EOF'
#!/bin/sh
count=0
[ ! -f "$TM_UPLOAD_COUNT" ] || count="$(cat "$TM_UPLOAD_COUNT")"
count=$((count + 1))
printf '%s\n' "$count" > "$TM_UPLOAD_COUNT"
[ -f "$1" ] || exit 2
[ "${TM_FAIL_UPLOAD:-0}" != 1 ] || exit 1
EOF
chmod +x "$fake/trafficexport" "$fake/trafficupload"

run_manager()
{
	TM_TRAFFICEXPORT="$fake/trafficexport" \
	TM_TRAFFICUPLOAD="$fake/trafficupload" \
	TM_LOGGER=: TM_EXPORT_COUNT="$export_count" TM_UPLOAD_COUNT="$upload_count" \
		sh "$manager" --month "$1" --journal-dir "$journal" \
		--archive-dir "$archive" --state-dir "$state"
}

run_manager 2026-09
grep -qx 'export_status=success' "$state/archive-2026-09.state"
grep -qx 'upload_status=success' "$state/archive-2026-09.state"
grep -q '^updated_at=' "$state/archive-2026-09.state"
count_is "$export_count" 1
count_is "$upload_count" 1

run_manager 2026-09
count_is "$export_count" 1
count_is "$upload_count" 1

if TM_FAIL_EXPORT=1 run_manager 2026-08; then fail 'export failure returned success'; fi
grep -qx 'export_status=failed' "$state/archive-2026-08.state"
grep -qx 'upload_status=not_attempted' "$state/archive-2026-08.state"
grep -qx 'error=export_failed_1' "$state/archive-2026-08.state"
count_is "$export_count" 2
count_is "$upload_count" 1

run_manager 2026-08
grep -qx 'upload_status=success' "$state/archive-2026-08.state"
count_is "$export_count" 3
count_is "$upload_count" 2

if TM_FAIL_UPLOAD=1 run_manager 2026-07; then fail 'upload failure returned success'; fi
grep -qx 'export_status=success' "$state/archive-2026-07.state"
grep -qx 'upload_status=failed' "$state/archive-2026-07.state"
grep -qx 'error=upload_failed_1' "$state/archive-2026-07.state"
count_is "$export_count" 4
count_is "$upload_count" 3

run_manager 2026-07
grep -qx 'upload_status=success' "$state/archive-2026-07.state"
count_is "$export_count" 4
count_is "$upload_count" 4

if run_manager 2026-13; then fail 'invalid month returned success'; fi

echo 'archive manager tests passed'
