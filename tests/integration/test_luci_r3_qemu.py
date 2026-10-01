"""Acceptance checks for the disposable ImmortalWrt QEMU r3 fixture only."""

from __future__ import annotations

import json
import os
import re
import shlex
import subprocess
import time
import urllib.parse
import urllib.request


BASE = "http://127.0.0.1:18080"
SSH_PORT = os.environ.get("TRAFFICMON_QEMU_SSH_PORT", "2224")
SSH = ["ssh", "-p", SSH_PORT, "-o", "BatchMode=yes", "root@127.0.0.1"]
KNOWN = "02:00:00:00:00:01"
UNKNOWN = "ip4:10.124.0.219"
HOSTILE = '<script>alert(1)</script>&"'


def guest(command: str) -> str:
    return subprocess.run(SSH + [command], check=True, capture_output=True,
                          text=True, timeout=20).stdout.strip()


def sample() -> dict:
    for _ in range(5):
        try:
            pid = guest("pidof trafficmon").split()[0]
            status = guest(f"cat /proc/{pid}/status")
            return {"pid": pid,
                    "VmRSS_kB": int(re.search(r"^VmRSS:\s+(\d+)", status, re.M).group(1)),
                    "VmHWM_kB": int(re.search(r"^VmHWM:\s+(\d+)", status, re.M).group(1)),
                    "FD": int(guest(f"ls /proc/{pid}/fd | wc -l"))}
        except (subprocess.CalledProcessError, IndexError):
            time.sleep(0.5)
    raise AssertionError("trafficmon process changed during RSS sampling")


def wait_trafficmon() -> None:
    for _ in range(30):
        try:
            if json.loads(guest("trafficctl status"))["collector_state"] == "running":
                return
        except (subprocess.CalledProcessError, json.JSONDecodeError):
            pass
        time.sleep(0.5)
    raise AssertionError("trafficmon did not become ready after QEMU-only restart")


def request(path: str, body: bytes | None = None) -> tuple[int, str]:
    with urllib.request.urlopen(BASE + path, data=body, timeout=45) as response:
        return response.status, response.read().decode("utf-8", errors="strict")


def rpc(sid: str, obj: str, method: str, args: dict) -> dict:
    payload = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "call",
                          "params": [sid, obj, method, args]}).encode()
    status, data = request("/ubus", payload)
    reply = json.loads(data)
    assert status == 200 and "error" not in reply and reply["result"][0] == 0, reply
    return reply["result"][1]


def main() -> None:
    assert "luci-app-trafficmon - 0.4.0-r3" in guest("opkg list-installed")
    assert json.loads(guest("trafficctl status"))["record_count"] == 10000
    baseline = sample()
    tunnel = subprocess.Popen(["ssh", "-p", SSH_PORT, "-o", "BatchMode=yes",
                               "-o", "ExitOnForwardFailure=yes", "-N", "-L",
                               "127.0.0.1:18080:127.0.0.1:80", "root@127.0.0.1"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        for _ in range(40):
            if tunnel.poll() is not None:
                raise AssertionError("QEMU HTTP tunnel exited")
            try:
                request("/luci-static/resources/view/trafficmon/overview.js")
                break
            except OSError:
                time.sleep(0.1)
        sid = rpc("0" * 32, "session", "login", {"username": "root",
                  "password": os.environ.get("TRAFFICMON_QEMU_PASSWORD", "")})["ubus_rpc_session"]

        def helper(*args: str) -> tuple[dict, float, int]:
            start = time.perf_counter()
            result = rpc(sid, "file", "exec", {"command": "/usr/libexec/trafficmon-luci",
                                               "params": list(args)})
            elapsed = (time.perf_counter() - start) * 1000
            assert result["code"] == 0, (args, result)
            return json.loads(result["stdout"]), elapsed, len(result["stdout"].encode())

        def export(period: str, format_name: str) -> tuple[str, float, int]:
            command = "/usr/libexec/trafficmon-luci export-" + format_name + " " + period
            body = urllib.parse.urlencode({"sessionid": sid, "command": command}).encode()
            start = time.perf_counter()
            status, data = request("/cgi-bin/cgi-exec", body)
            assert status == 200, (status, data[:200])
            return data, (time.perf_counter() - start) * 1000, len(data.encode())

        for page in ("overview", "devices", "detail", "history", "settings"):
            status, js = request(f"/luci-static/resources/view/trafficmon/{page}.js")
            assert status == 200 and "view.extend" in js, page
        status, history_js = request("/luci-static/resources/view/trafficmon/history.js")
        assert status == 200 and "10,000" in history_js
        print("PASS r3 five page assets and explicit 10,000-record export warning")

        periods, _, _ = helper("periods")
        assert [p["start"] for p in periods["periods"]] == ["2026-09-01", "2026-08-01"]
        overview, overview_ms, overview_bytes = helper("summary", "current")
        diagnostics, _, _ = helper("status")
        assert overview["period"] == "2026-09-01" and overview["records"]
        for field in ("record_count", "collector_state", "netlink_enobufs",
                      "netlink_parse_errors", "daemon_version", "netlink_receive_buffer"):
            assert field in diagnostics, field
        print(f"PASS Overview totals/period/diagnostics; RPC {overview_ms:.1f} ms, "
              f"{overview_bytes} bytes")

        first, first_ms, first_bytes = helper("devices", "current", "total", "desc",
                                               "50", "0", "")
        second, second_ms, second_bytes = helper("devices", "current", "total", "desc",
                                                  "50", "50", "")
        assert first["total_count"] == second["total_count"] == 500
        assert len(first["devices"]) == len(second["devices"]) == 50
        assert {row["device_id"] for row in first["devices"]}.isdisjoint(
            row["device_id"] for row in second["devices"])
        assert first["devices"][0]["total_bytes"] >= first["devices"][-1]["total_bytes"]
        ascending, _, _ = helper("devices", "current", "total", "asc", "50", "0", "")
        assert ascending["devices"][0]["total_bytes"] <= ascending["devices"][-1]["total_bytes"]
        found, search_ms, search_bytes = helper("devices", "current", "total", "desc",
                                                "50", "0", KNOWN)
        assert found["total_count"] == 1 and found["devices"][0]["device_id"] == KNOWN
        assert any("10.123." in address for row in first["devices"]
                   for address in row["addresses"])
        assert any("fd12:" in address for row in first["devices"]
                   for address in row["addresses"])
        print(f"PASS 500-device sort/search/pagination/IPv4/IPv6; first page "
              f"{first_ms:.1f} ms/{first_bytes} bytes, page 2 {second_ms:.1f} ms/"
              f"{second_bytes} bytes, search {search_ms:.1f} ms/{search_bytes} bytes")

        device, detail_ms, detail_bytes = helper("device", "current", KNOWN)
        breakdown, _, _ = helper("detail", "current", KNOWN)
        one = device["devices"][0]
        assert one["connections"] == 21 and len(one["addresses"]) == 21
        assert sum(":" in address for address in one["addresses"]) == 20
        assert {row["protocol"] for row in breakdown["devices"]} == {
            "HTTPS", "HTTP", "DNS", "NTP", "Other"}
        assert sum(row["download_bytes"] for row in breakdown["devices"]) == one["download_bytes"]
        assert sum(row["upload_bytes"] for row in breakdown["devices"]) == one["upload_bytes"]
        assert sum(row["connections"] for row in breakdown["devices"]) == one["connections"]
        unknown, _, _ = helper("device", "current", UNKNOWN)
        assert unknown["devices"][0]["mac"] is None
        print(f"PASS Device Detail IPv4 + 20 IPv6, five protocols, totals and unknown MAC; "
              f"device RPC {detail_ms:.1f} ms/{detail_bytes} bytes")

        historical, _, _ = helper("devices", "2026-08-01", "total", "desc", "50", "0", "")
        assert historical["total_count"] == 2
        old_detail, _, _ = helper("detail", "2026-08-01", KNOWN)
        assert old_detail["total_count"] == 2
        current_again, _, _ = helper("devices", "current", "total", "desc", "1", "0", "")
        assert current_again["total_count"] == 500
        print("PASS historical period/list/device detail and switch back to current")

        during = sample()
        for period, expected in (("current", 10000), ("2026-08-01", 3)):
            data, elapsed, size = export(period, "json")
            rows = json.loads(data)["records"]
            assert len(rows) == expected, (period, len(rows))
            print(f"PASS {period} JSON export {len(rows)} rows, {elapsed:.1f} ms/{size} bytes")
            data, elapsed, size = export(period, "csv")
            assert len(data.splitlines()) == expected + 1
            print(f"PASS {period} CSV export {expected} rows, {elapsed:.1f} ms/{size} bytes")

        # UCI static host metadata is consumed by trafficmon; no DHCP service reload occurs.
        guest("uci set dhcp.phase4xss=host")
        guest("uci set " + shlex.quote("dhcp.phase4xss.mac=" + KNOWN))
        guest("uci set " + shlex.quote("dhcp.phase4xss.name=" + HOSTILE))
        guest("uci commit dhcp")
        guest("/etc/init.d/trafficmon restart")
        wait_trafficmon()
        try:
            hostile, _, _ = helper("device", "current", KNOWN)
            assert hostile["devices"][0]["hostname"] == HOSTILE
            print("PASS QEMU hostile hostname reaches authenticated API unchanged as text data")
        finally:
            guest("uci -q delete dhcp.phase4xss; uci commit dhcp")
            guest("/etc/init.d/trafficmon restart")
            wait_trafficmon()

        after = sample()
        print("RSS_FD before=" + json.dumps(baseline) + " during=" + json.dumps(during) +
              " after=" + json.dumps(after))
    finally:
        tunnel.terminate()
        try:
            tunnel.wait(timeout=3)
        except subprocess.TimeoutExpired:
            tunnel.kill()


if __name__ == "__main__":
    main()
