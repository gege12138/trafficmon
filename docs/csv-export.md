# Connection journal CSV export

`trafficexport` reads Connection Journal v1 shards directly. It does not contact the daemon or
change journal, IPC, or period database data.

Export a UTC calendar month:

```sh
trafficexport --month 2026-09
```

This reads `/var/lib/trafficmon/journal/journal-20260901.bin` through
`journal-20260930.bin` and atomically replaces `trafficmon-2026-09.csv` after a successful
export. Paths can be selected explicitly:

```sh
trafficexport \
  --journal-dir /var/lib/trafficmon/journal \
  --start 2026-09-10 \
  --end 2026-09-20 \
  --output /tmp/trafficmon-range.csv
```

The date range is inclusive. Missing daily shards and header-only shards are valid and contribute
no rows. A shard with an invalid header is logged and skipped. Records with an invalid CRC or
invalid fields are counted, logged per shard, and skipped while later fixed-size records continue
to export. Other read or output errors fail the command and leave the previous output untouched.

The output is UTF-8-compatible RFC 4180-style CSV with CRLF line endings:

```text
timestamp,device_id,src_ip,dst_ip,src_port,dst_port,protocol,rx_bytes,tx_bytes
```

`timestamp` is the journal Unix timestamp in seconds. `protocol` is the IP protocol number.
Text fields are quoted when needed; embedded quotes are doubled. Export is streaming and retains
only one journal record plus standard I/O buffers in memory.
