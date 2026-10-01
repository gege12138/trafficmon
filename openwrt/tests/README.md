# OpenWrt runtime gates

Run `smoke.sh`, `service.sh`, `reboot-persistence.sh`, and
`upgrade-preservation.sh` on the OpenWrt router. `forwarding-host.sh` runs on the Linux topology
host and expects a client namespace plus passwordless SSH access to the router. Its defaults
match the documented two-NIC Phase 3B test topology and can be overridden with `CLIENT_NS`,
`ROUTER`, `WAN4_URL`, `WAN6_URL`, `CLIENT4`, and `CLIENT6_PREFIX`.

For reboot validation:

```sh
/tmp/reboot-persistence.sh before
reboot
# after the router and trafficmon are ready
/tmp/reboot-persistence.sh after
```

The scripts fail on the first unmet assertion and print `PASS` only after all checks complete.
