#!/bin/sh
set -eu

[ "$#" -eq 3 ] || exit 2
read_helper="$1"
action_helper="$2"
work_directory="$3"
state_directory="$work_directory/state"
archive_directory="$work_directory/archive"
command_log="$work_directory/commands.log"
rm -rf "$work_directory"
mkdir -p "$state_directory" "$archive_directory" "$work_directory/bin"

cat > "$state_directory/archive-2026-08.state" <<'EOF'
version=1
month=2026-08
export_status=success
upload_status=success
updated_at=2026-09-01T00:00:00Z
error=none
EOF
cat > "$state_directory/archive-2026-09.state" <<'EOF'
version=1
month=2026-09
export_status=success
upload_status=failed
updated_at=2026-10-01T00:00:00Z
error=upload_failed_1
EOF
printf 'timestamp,device_id\n' > "$archive_directory/trafficmon-2026-09.csv"

cat > "$work_directory/bin/archive" <<'EOF'
#!/bin/sh
printf 'archive:%s\n' "$#" >> "$TM_TEST_COMMAND_LOG"
exit "${TM_TEST_ARCHIVE_RESULT:-0}"
EOF
cat > "$work_directory/bin/upload" <<'EOF'
#!/bin/sh
printf 'upload:%s:%s\n' "$#" "$1" >> "$TM_TEST_COMMAND_LOG"
exit "${TM_TEST_UPLOAD_RESULT:-0}"
EOF
chmod +x "$work_directory/bin/archive" "$work_directory/bin/upload"

status="$(TRAFFICMON_LUCI_TESTING=1 TM_LUCI_ARCHIVE_STATE_DIRECTORY="$state_directory" \
	sh "$read_helper" archive-status)"
printf '%s\n' "$status" | grep -q '"month":"2026-09"'
printf '%s\n' "$status" | grep -q '"upload_status":"failed"'

TRAFFICMON_LUCI_TESTING=1 TM_LUCI_ARCHIVE_STATE_DIRECTORY="$state_directory" \
	TM_LUCI_ARCHIVE_DIRECTORY="$archive_directory" TM_LUCI_TRAFFICARCHIVE="$work_directory/bin/archive" \
	TM_LUCI_TRAFFICUPLOAD="$work_directory/bin/upload" TM_TEST_COMMAND_LOG="$command_log" \
	sh "$action_helper" run >/dev/null
grep -q '^archive:0$' "$command_log"

TRAFFICMON_LUCI_TESTING=1 TM_LUCI_ARCHIVE_STATE_DIRECTORY="$state_directory" \
	TM_LUCI_ARCHIVE_DIRECTORY="$archive_directory" TM_LUCI_TRAFFICARCHIVE="$work_directory/bin/archive" \
	TM_LUCI_TRAFFICUPLOAD="$work_directory/bin/upload" TM_TEST_COMMAND_LOG="$command_log" \
	sh "$action_helper" retry >/dev/null
grep -q "^upload:1:$archive_directory/trafficmon-2026-09.csv$" "$command_log"
grep -q '^upload_status=success$' "$state_directory/archive-2026-09.state"
grep -q '^error=none$' "$state_directory/archive-2026-09.state"

if TRAFFICMON_LUCI_TESTING=1 TM_LUCI_ARCHIVE_STATE_DIRECTORY="$state_directory" \
	sh "$action_helper" retry ../../etc/passwd >"$work_directory/out" 2>"$work_directory/error"; then
	echo 'expected user-supplied path to be rejected' >&2
	exit 1
fi
grep -q '无效的归档操作请求' "$work_directory/error"

if TRAFFICMON_LUCI_TESTING=1 TM_LUCI_ARCHIVE_STATE_DIRECTORY="$state_directory" \
	TM_LUCI_ARCHIVE_DIRECTORY="$archive_directory" TM_LUCI_TRAFFICARCHIVE="$work_directory/bin/archive" \
	TM_LUCI_TRAFFICUPLOAD="$work_directory/bin/upload" TM_TEST_COMMAND_LOG="$command_log" \
	TM_TEST_ARCHIVE_RESULT=1 sh "$action_helper" run >"$work_directory/out" 2>"$work_directory/error"; then
	echo 'expected archive command failure' >&2
	exit 1
fi
grep -q '归档执行失败' "$work_directory/error"

echo 'PASS LuCI archive state read, fixed command invocation, retry and failure handling'
