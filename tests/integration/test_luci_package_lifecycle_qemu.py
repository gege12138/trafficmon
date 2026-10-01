"""Disposable ImmortalWrt QEMU test of LuCI opkg removal and reinstallation.

Requires trafficmon and its database fixture, but no LuCI package initially.
Set TRAFFICMON_QEMU_SSH_PORT and TRAFFICMON_QEMU_LUCI_IPK as needed. The IPK
path is inside the guest, normally copied to /tmp before running this test.
"""

from __future__ import annotations

import json
import os
import subprocess
import time
import urllib.parse
import urllib.request


SSH_PORT = os.environ.get("TRAFFICMON_QEMU_SSH_PORT", "2224")
IPK = os.environ.get("TRAFFICMON_QEMU_LUCI_IPK", "/tmp/luci-app-trafficmon_0.4.0-r4_x86_64.ipk")
BASE = "http://127.0.0.1:18080"
SSH = ["ssh", "-p", SSH_PORT, "-o", "BatchMode=yes", "root@127.0.0.1"]
ACL = "/usr/share/rpcd/acl.d/luci-app-trafficmon.json"
FILES = "/etc/config/trafficmon /etc/trafficmon/protocols /etc/trafficmon/*.db"


def guest(command: str) -> str:
    return subprocess.run(SSH + [command], check=True, capture_output=True,
                          text=True, timeout=45).stdout.strip()


def rpc(sid: str, obj: str, method: str, args: dict) -> tuple[int, dict]:
    body = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "call",
                       "params": [sid, obj, method, args]}).encode()
    with urllib.request.urlopen(BASE + "/ubus", data=body, timeout=45) as response:
        reply = json.load(response)
    assert "error" not in reply, reply
    result = reply["result"]
    return result[0], result[1] if len(result) > 1 else {}


def login() -> str:
    code, result = rpc("0" * 32, "session", "login", {
        "username": "root", "password": os.environ.get("TRAFFICMON_QEMU_PASSWORD", "")
    })
    assert code == 0, (code, result)
    return result["ubus_rpc_session"]


def helper(sid: str, *args: str) -> str:
    code, result = rpc(sid, "file", "exec", {
        "command": "/usr/libexec/trafficmon-luci", "params": list(args)
    })
    assert code == 0, ("authenticated file.exec denied", code, args, result)
    assert result["code"] == 0, (args, result)
    return result["stdout"]


def export(sid: str, period: str, format_name: str) -> str:
    # The UI uses fs.exec_direct/CGI for large exports. rpcd file.exec has a
    # bounded output buffer and returns ubus status 8 above that limit.
    command = "/usr/libexec/trafficmon-luci export-" + format_name + " " + period
    body = urllib.parse.urlencode({"sessionid": sid, "command": command}).encode()
    with urllib.request.urlopen(BASE + "/cgi-bin/cgi-exec", data=body,
                                timeout=45) as response:
        assert response.status == 200
        return response.read().decode("utf-8")


def check_queries(sid: str) -> None:
    access_code, access = rpc(sid, "session", "access", {
        "scope": "file", "object": "/usr/libexec/trafficmon-luci status", "function": "exec"
    })
    assert access_code == 0 and access["access"] is True
    assert json.loads(helper(sid, "status"))["collector_state"] == "running"
    json.loads(helper(sid, "summary", "current"))
    json.loads(helper(sid, "devices", "current", "total", "desc", "50", "0", ""))
    json.loads(helper(sid, "detail", "current", "02:00:00:00:00:01"))
    periods = json.loads(helper(sid, "periods"))
    assert "2026-08-01" in json.dumps(periods)
    json.loads(helper(sid, "devices", "2026-08-01", "total", "desc", "50", "0", ""))
    json.loads(helper(sid, "detail", "2026-08-01", "02:00:00:00:00:01"))
    code, config = rpc(sid, "uci", "get", {"config": "trafficmon"})
    assert code == 0 and config
    exported = json.loads(export(sid, "current", "json"))
    assert len(exported["records"]) == 10000
    csv = export(sid, "current", "csv")
    assert len(csv.splitlines()) == 10001


def main() -> None:
    assert "luci-app-trafficmon - " not in guest("opkg list-installed")
    before = guest("sha256sum " + FILES)
    daemon_pid = guest("pidof trafficmon")
    service_pids = {name: guest("pidof " + name)
                    for name in ("netifd", "uhttpd", "dnsmasq")}
    assert json.loads(guest("trafficctl status"))["collector_state"] == "running"
    guest("opkg install " + IPK)
    assert "luci-app-trafficmon - 0.4.0-r4" in guest("opkg list-installed")
    acl_hash = guest("sha256sum " + ACL)
    assert "486da41a07764658d2936d3c78f8e2083e000741f849da55d088752bb155b3ce" in acl_hash

    tunnel = subprocess.Popen([
        "ssh", "-p", SSH_PORT, "-o", "BatchMode=yes", "-o", "ExitOnForwardFailure=yes",
        "-N", "-L", "127.0.0.1:18080:127.0.0.1:80", "root@127.0.0.1"
    ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        for _ in range(40):
            if tunnel.poll() is not None:
                raise AssertionError("QEMU HTTP tunnel exited")
            try:
                with urllib.request.urlopen(BASE + "/luci-static/resources/view/trafficmon/overview.js",
                                            timeout=2) as response:
                    assert response.status == 200
                break
            except OSError:
                time.sleep(0.2)
        else:
            raise AssertionError("QEMU HTTP tunnel not ready")

        old_sid = login()
        check_queries(old_sid)
        print("PASS r4 initial install and authenticated Overview/Devices/Detail/History/Settings/exports")

        # These are only LuCI's own caches in a disposable QEMU guest.
        guest("mkdir -p /tmp/luci-modulecache && touch /tmp/luci-indexcache.trafficmon-test "
              "/tmp/luci-modulecache/trafficmon-test")
        guest("opkg remove luci-app-trafficmon")
        assert guest("test ! -e " + ACL + " && echo absent") == "absent"
        assert json.loads(guest("trafficctl status"))["collector_state"] == "running"
        assert guest("sha256sum " + FILES) == before
        assert guest("pidof trafficmon") == daemon_pid
        assert {name: guest("pidof " + name) for name in service_pids} == service_pids
        print("PASS removal leaves daemon, CLI, UCI, protocols, and databases intact")

        guest("opkg install " + IPK)
        assert guest("sha256sum " + ACL) == acl_hash
        assert guest("test ! -e /tmp/luci-indexcache.trafficmon-test && "
                     "test ! -e /tmp/luci-modulecache/trafficmon-test && echo cleared") == "cleared"
        old_code, _ = rpc(old_sid, "file", "exec", {
            "command": "/usr/libexec/trafficmon-luci", "params": ["status"]
        })
        new_sid = login()
        check_queries(new_sid)
        with urllib.request.urlopen(BASE + "/luci-static/resources/view/trafficmon/overview.js",
                                    timeout=5) as response:
            assert response.status == 200
        assert guest("sha256sum " + FILES) == before
        assert guest("pidof trafficmon") == daemon_pid
        assert {name: guest("pidof " + name) for name in service_pids} == service_pids
        print(f"PASS reinstall without reboot: old-session RPC code={old_code}; "
              "new authenticated RPC code=0; LuCI caches cleared")
    finally:
        tunnel.terminate()
        try:
            tunnel.wait(timeout=3)
        except subprocess.TimeoutExpired:
            tunnel.kill()


if __name__ == "__main__":
    main()
