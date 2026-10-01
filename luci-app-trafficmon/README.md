# luci-app-trafficmon

Separate LuCI package without architecture-specific binaries for ImmortalWrt 24.10.4 (IPK) and OpenWrt
25.12.x (APK). Requires `luci-base` and `trafficmon`; the headless daemon has no LuCI dependency.

Stage with `./luci-app-trafficmon/prepare-sdk-package.sh /path/to/sdk`, then build using the
SDK's `make package/luci-app-trafficmon/compile NO_DEPS=1`. The read ACL permits execution of
only `/usr/libexec/trafficmon-luci`, a read-only argument-validated wrapper around trafficctl;
write ACL permits only UCI `trafficmon`. No browser access to the Unix socket, shell, or file
system is granted. LuCI's normal UCI apply invokes the existing trafficmon-only procd trigger;
it must never reload network/firewall/DNS services.

Pages: overview, devices, device detail, history, settings/diagnostics. JSON/CSV export uses
the backend's exact raw query. Historical hostnames, when displayed, are *current metadata*,
not a historical snapshot. Device address lists are capped at 32 by the backend. No live graphs
or high-frequency polling are used. No package removal script touches daemon configuration or
databases.

Production deployment: NOT PERFORMED — intentionally deferred for remote-router safety.
