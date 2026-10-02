# Monthly archive manager

`trafficarchive` is a one-shot coordinator for the completed previous calendar month:

```text
trafficexport -> trafficmon-YYYY-MM.csv -> trafficupload-uci -> SMB/NAS
```

It is not part of the trafficmon daemon. On OpenWrt, `/etc/init.d/trafficarchive`
registers one fixed cron entry which runs at 03:17 every day. Daily invocation allows a failed
archive to retry after network recovery; the per-month success state makes later invocations no-ops.

The feature is disabled by default:

```uci
config archive 'monthly'
        option enabled '0'
        option archive_directory '/var/lib/trafficmon/archive'
        option state_directory '/var/lib/trafficmon/archive/state'
```

Each `archive-YYYY-MM.state` file is atomically replaced and contains:

```text
version=1
month=2026-09
export_status=success
upload_status=success
updated_at=2026-10-01T03:17:00Z
error=none
```

Export writes to a same-directory temporary CSV and renames it only on success. If upload fails,
the successful CSV and export state are retained, so the next invocation retries upload without
regenerating the CSV. A lock directory prevents concurrent runs for the same month.
