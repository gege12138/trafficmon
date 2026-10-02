# Journal retention

`trafficretention` performs one bounded cleanup pass over the connection journal directory. It
only recognizes exact `journal-YYYYMMDD.bin` names whose date is a valid calendar date. Other
entries are ignored and are never passed to a deletion operation.

The default directory is `/var/lib/trafficmon/journal` and the default retention interval is 120
days. A shard is deleted only when its date plus the configured interval is strictly before the
current UTC date. Therefore, a shard exactly 120 days old is retained when the interval is 120.

```text
trafficretention [--journal-dir DIR] [--retention-days DAYS]
```

OpenWrt exposes the interval as `journal_retention_days` in `/etc/config/trafficmon`. The init
script runs one cleanup pass before starting the daemon. Cleanup errors are logged and explicitly
do not prevent the daemon from starting. There is no scheduler in this phase.
