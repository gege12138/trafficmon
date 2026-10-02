# Connection Journal v1

The connection journal is independent of the period database and IPC protocol. It is disabled
unless `trafficmon` receives `--journal-dir DIR`. On OpenWrt, set `journal_enabled` to `1`; the
default directory is `/var/lib/trafficmon/journal`.

One record is emitted for each accepted live conntrack `DELETE` event after local-device
classification and identity resolution. Dump records and `NEW` events are not journaled, so a
record represents the final counters observed for a completed connection.

Files are UTC daily shards named `journal-YYYYMMDD.bin`. All integers use little-endian byte order.

## File header (16 bytes)

| Offset | Size | Value |
|---:|---:|---|
| 0 | 8 | `TMJRNL1\0` |
| 8 | 2 | format version (`1`) |
| 10 | 2 | header size (`16`) |
| 12 | 2 | record size (`128`) |
| 14 | 2 | reserved (`0`) |

## Record (128 bytes)

| Offset | Size | Value |
|---:|---:|---|
| 0 | 4 | `JREC` |
| 4 | 2 | format version (`1`) |
| 6 | 2 | record size (`128`) |
| 8 | 8 | Unix timestamp, UTC |
| 16 | 1 | address family (`4` or `6`) |
| 17 | 1 | IP protocol number |
| 18 | 2 | source port |
| 20 | 2 | destination port |
| 22 | 1 | device ID length |
| 23 | 1 | reserved |
| 24 | 16 | source address, IPv4 uses the first 4 bytes |
| 40 | 16 | destination address, IPv4 uses the first 4 bytes |
| 56 | 8 | received bytes relative to the local device |
| 64 | 8 | transmitted bytes relative to the local device |
| 72 | 52 | device ID, not NUL-terminated on disk |
| 124 | 4 | CRC-32 of bytes 0 through 123 |

On opening a shard, a trailing partial or invalid record is truncated to the last valid record.
An invalid header is never overwritten. Journal failures are logged and do not terminate the
daemon.
