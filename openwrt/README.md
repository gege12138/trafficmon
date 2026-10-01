# OpenWrt 25.12 and ImmortalWrt 24.10 package

The package targets OpenWrt 25.12.x (APK) and ImmortalWrt 24.10.4 (IPK) from the same source
tree and uses each distribution's normal SDK package build.
It has no libnl dependency: the collector uses a raw `NETLINK_NETFILTER` socket. The only
package dependency is `kmod-nf-conntrack-netlink`.

## SDK build

Download and verify the official SDK, extract it, then stage this repository into it:

```sh
./openwrt/prepare-sdk-package.sh /path/to/openwrt-sdk
make -C /path/to/openwrt-sdk defconfig
make -C /path/to/openwrt-sdk package/trafficmon/compile V=s
```

The package definition is `openwrt/trafficmon/Makefile`. `prepare-sdk-package.sh` deliberately
copies the required source snapshot into `package/trafficmon/source`; the SDK still owns all
toolchain selection, dependency resolution, compilation, stripping, and package creation.
For the exact ImmortalWrt target selection and opkg gates, see `../immortalwrt/README.md`.

## Configuration and storage

`/etc/config/trafficmon` supports `enabled`, `refresh_interval`, `netlink_buffer_size`,
`database_directory`, `database_generations`, `database_limit`, `commit_interval`,
`accounting_period`, `protocol_database`, `socket_path`, `debug`, and repeated `local_network`
values.

`local_network` accepts an explicit IPv4/IPv6 CIDR or an OpenWrt logical network such as `lan`.
The procd script uses `/lib/functions/network.sh` to resolve all current IPv4 and IPv6 subnets.
It also passes the resolved local device to the daemon for interface-scoped ARP/NDP lookup.
For explicit CIDRs without a logical network, configure a trusted `local_interface` list entry.
An iface hotplug hook reloads the service after the selected logical network changes, so a new
delegated IPv6 prefix does not require a reboot. Resolution failure is fatal; trafficmon never
falls back to an unintended global accounting scope.

`database_prealloc` is not implemented because the database writer creates a bounded encoded
snapshot and atomically replaces the old file; it does not use a preallocated mutable database.

The default database path, `/etc/trafficmon`, is on OpenWrt's persistent overlay. This preserves
history across reboot but causes one atomic database write per `commit_interval` (default one
day), plus clean shutdown and rollover. OpenWrt maps `/var` to volatile tmpfs, so paths under
`/var` are not persistent on this platform. Set `database_directory` to `/tmp/trafficmon` (or a
path below `/var`) to avoid flash writes, with the explicit tradeoff that all accounting data is
lost on reboot.

The UCI config is a package conffile. `/usr/share/trafficmon/protocols` is the packaged default
template; on first start the init script copies it to `/etc/trafficmon/protocols`, which is the
default configured protocol database. The runtime copy is not owned by the package, so upgrades
can refresh the template without overwriting user protocol configuration. The package has no
removal script and does not delete `/etc/trafficmon`, its database, or its runtime protocol copy.

procd runs the daemon in the foreground, captures stdout/stderr into logd, requests respawn after
abnormal exit, and lets SIGTERM trigger the daemon's final atomic commit. Normal service stop is
not treated as a crash.
