"""HTTP/RPC smoke test against the disposable QEMU guest forwarded at VPS port 2224."""

from __future__ import annotations

import http.cookiejar
import json
import os
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request


BASE = "http://127.0.0.1:18080"
SSH_PORT = os.environ.get("TRAFFICMON_QEMU_SSH_PORT", "2224")


def request(path: str, data: bytes | None = None, opener=None) -> tuple[int, str]:
    client = opener.open if opener else urllib.request.urlopen
    try:
        with client(BASE + path, data=data, timeout=8) as reply:
            return reply.status, reply.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read().decode("utf-8", errors="replace")


def rpc(sid: str, obj: str, method: str, args: dict) -> dict:
    payload = json.dumps({
        "jsonrpc": "2.0", "id": 1, "method": "call", "params": [sid, obj, method, args]
    }).encode()
    status, text = request("/ubus", payload)
    assert status == 200, (status, text[:300])
    reply = json.loads(text)
    assert "error" not in reply, reply
    code = reply["result"][0]
    result = reply["result"][1] if len(reply["result"]) > 1 else {}
    assert code == 0, reply
    return result


def main() -> None:
    tunnel = subprocess.Popen([
        "ssh", "-p", SSH_PORT, "-o", "ExitOnForwardFailure=yes", "-o", "StrictHostKeyChecking=no",
        "-N", "-L", "127.0.0.1:18080:127.0.0.1:80", "root@127.0.0.1"
    ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        for _ in range(40):
            if tunnel.poll() is not None:
                raise AssertionError("SSH tunnel exited: " + tunnel.stderr.read().decode())
            try:
                request("/luci-static/resources/view/trafficmon/overview.js")
                break
            except OSError:
                time.sleep(0.1)
        for page in ("overview", "devices", "detail", "history", "settings"):
            status, content = request(f"/luci-static/resources/view/trafficmon/{page}.js")
            assert status == 200 and "view.extend" in content, (page, status)
        print("PASS five LuCI JS views served by uhttpd")

        guest_password = os.environ.get("TRAFFICMON_QEMU_PASSWORD", "")
        login = rpc("00000000000000000000000000000000", "session", "login",
                    {"username": "root", "password": guest_password})
        sid = login["ubus_rpc_session"]
        def helper(*args: str) -> dict:
            return rpc(sid, "file", "exec", {
                "command": "/usr/libexec/trafficmon-luci", "params": list(args)
            })

        for args in (("status",), ("periods",), ("summary", "current"),
                     ("devices", "current", "total", "desc", "50", "0", ""),
                     ("detail", "current", "ip4:10.0.2.15")):
            result = helper(*args)
            assert result["code"] == 0, (args, result)
            json.loads(result["stdout"])
        for format_name in ("json", "csv"):
            command = "/usr/libexec/trafficmon-luci export-" + format_name + " current"
            body = urllib.parse.urlencode({"sessionid": sid, "command": command}).encode()
            status, data = request("/cgi-bin/cgi-exec", body)
            assert status == 200, (format_name, status, data[:200])
            if format_name == "json":
                json.loads(data)
            else:
                assert data.splitlines(), data[:200]
        print("PASS authenticated RPC status/list/summary/device/detail and CGI JSON/CSV")

        for args in (("devices", "bad", "total", "desc", "50", "0", ""),
                     ("devices", "current", "bad", "desc", "50", "0", ""),
                     ("devices", "current", "total", "desc", "999999", "0", ""),
                     ("devices", "current", "total", "desc", "-1", "0", ""),
                     ("devices", "current", "total", "desc", "50", "0", "x" * 65),
                     ("devices", "current", "total", "desc", "50", "0", "é"),
                     ("detail", "current", "not-a-device")):
            result = helper(*args)
            assert result["code"] != 0, (args, result)
        print("PASS malformed input rejected by authenticated helper")

        jar = http.cookiejar.CookieJar()
        opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar))
        body = urllib.parse.urlencode({"luci_username": "root", "luci_password": guest_password}).encode()
        status, html = request("/cgi-bin/luci/", body, opener)
        assert status == 200 and "LuCI" in html, (status, html[:300])
        status, menu_text = request("/cgi-bin/luci/admin/menu", opener=opener)
        assert status == 200 and "trafficmon" in menu_text.lower(), (status, menu_text[:300])
        for page in ("overview", "devices", "detail", "history", "settings"):
            status, html = request("/cgi-bin/luci/admin/status/trafficmon/" + page,
                                   opener=opener)
            assert status == 200 and "LuCI" in html, (page, status, html[:300])
        print("PASS authenticated LuCI menu contains Traffic Monitor")
        value = rpc(sid, "uci", "get", {"config": "trafficmon", "section": "main",
                                         "option": "refresh_interval"})
        original = value["value"]
        def pid(service: str) -> str:
            return subprocess.run(["ssh", "-p", SSH_PORT, "root@127.0.0.1", "pidof", service],
                                  capture_output=True, text=True, check=False).stdout.strip()
        before = {name: pid(name) for name in ("netifd", "uhttpd", "dnsmasq", "trafficmon")}
        changed = "16" if original != "16" else "17"
        def save(value: str) -> None:
            rpc(sid, "uci", "set", {"config": "trafficmon", "section": "main",
                                    "values": {"refresh_interval": value}})
            rpc(sid, "uci", "apply", {"rollback": False})
        try:
            save(changed)
            time.sleep(3)
            assert rpc(sid, "uci", "get", {"config": "trafficmon", "section": "main",
                                           "option": "refresh_interval"})["value"] == changed
            after = {name: pid(name) for name in before}
            assert after["trafficmon"] and after["trafficmon"] != before["trafficmon"], (before, after)
            for name in ("netifd", "uhttpd", "dnsmasq"):
                assert before[name] == after[name], (name, before, after)
            print("PASS UCI save/apply reloads trafficmon only; network/HTTP/DNS PIDs stable")
        finally:
            save(original)
            time.sleep(2)
    finally:
        tunnel.terminate()
        try:
            tunnel.wait(timeout=3)
        except subprocess.TimeoutExpired:
            tunnel.kill()


if __name__ == "__main__":
    main()
