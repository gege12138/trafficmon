#!/usr/bin/env python3
"""Opt-in privileged Linux conntrack smoke test.

Run with: sudo env TRAFFICMON_RUN_PRIVILEGED=1 ctest -L conntrack --output-on-failure
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path

NAMESPACES = ("tm_client", "tm_client2", "tm_router", "tm_server")


def run(*args: str, check: bool = True, capture: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, check=check, text=True, capture_output=capture, timeout=20)


def netns(namespace: str, *args: str, **kwargs: object) -> subprocess.CompletedProcess[str]:
    return run("ip", "netns", "exec", namespace, *args, **kwargs)


def cleanup(processes: list[subprocess.Popen[str]]) -> None:
    for process in processes:
        if process.poll() is None:
            process.terminate()
    for process in processes:
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
    for namespace in NAMESPACES:
        run("ip", "netns", "del", namespace, check=False, capture=True)


def main() -> int:
    if os.name != "posix" or os.geteuid() != 0 or os.environ.get(
        "TRAFFICMON_RUN_PRIVILEGED"
    ) != "1":
        print("SKIP: requires root and TRAFFICMON_RUN_PRIVILEGED=1")
        return 77
    parser = argparse.ArgumentParser()
    parser.add_argument("--daemon", required=True, type=Path)
    parser.add_argument("--cli", required=True, type=Path)
    parser.add_argument("--workdir", required=True, type=Path)
    args = parser.parse_args()
    daemon = args.daemon.resolve()
    cli = args.cli.resolve()
    workdir = args.workdir.resolve()
    if workdir.exists():
        shutil.rmtree(workdir)
    (workdir / "database").mkdir(parents=True)
    protocols = workdir / "protocols.txt"
    protocols.write_text("50 HTTP tcp 8080\n50 HTTP tcp 8081\n", encoding="ascii")
    socket_path = workdir / "trafficmon.sock"
    processes: list[subprocess.Popen[str]] = []
    cleanup([])
    try:
        for namespace in NAMESPACES:
            run("ip", "netns", "add", namespace)
        run("ip", "link", "add", "tm_c", "type", "veth", "peer", "name", "tm_rl")
        run("ip", "link", "add", "tm_c2", "type", "veth", "peer", "name", "tm_rl2")
        run("ip", "link", "add", "tm_s", "type", "veth", "peer", "name", "tm_rw")
        for interface, namespace in (("tm_c", "tm_client"), ("tm_rl", "tm_router"),
                                     ("tm_c2", "tm_client2"), ("tm_rl2", "tm_router"),
                                     ("tm_s", "tm_server"), ("tm_rw", "tm_router")):
            run("ip", "link", "set", interface, "netns", namespace)
        addresses = (
            ("tm_client", "tm_c", "192.168.50.2/24", "fd50::2/64"),
            ("tm_router", "tm_rl", "192.168.50.1/24", "fd50::1/64"),
            ("tm_client2", "tm_c2", "192.168.51.2/24", "fd52::2/64"),
            ("tm_router", "tm_rl2", "192.168.51.1/24", "fd52::1/64"),
            ("tm_router", "tm_rw", "198.51.100.1/24", "fd51::1/64"),
            ("tm_server", "tm_s", "198.51.100.2/24", "fd51::2/64"),
        )
        for namespace, interface, ipv4, ipv6 in addresses:
            run("ip", "-n", namespace, "addr", "add", ipv4, "dev", interface)
            run("ip", "-n", namespace, "addr", "add", ipv6, "dev", interface, "nodad")
            run("ip", "-n", namespace, "link", "set", "lo", "up")
            run("ip", "-n", namespace, "link", "set", interface, "up")
        run("ip", "-n", "tm_client", "-6", "addr", "add", "fd50::22/64", "dev", "tm_c",
            "nodad")
        run("ip", "-n", "tm_client", "route", "add", "default", "via", "192.168.50.1")
        run("ip", "-n", "tm_client", "-6", "route", "add", "default", "via", "fd50::1")
        run("ip", "-n", "tm_client2", "route", "add", "default", "via", "192.168.51.1")
        run("ip", "-n", "tm_client2", "-6", "route", "add", "default", "via", "fd52::1")
        run("ip", "-n", "tm_server", "route", "add", "default", "via", "198.51.100.1")
        run("ip", "-n", "tm_server", "-6", "route", "add", "default", "via", "fd51::1")
        netns("tm_router", "sysctl", "-q", "-w", "net.ipv4.ip_forward=1")
        netns("tm_router", "sysctl", "-q", "-w", "net.ipv6.conf.all.forwarding=1")
        netns("tm_router", "sysctl", "-q", "-w", "net.netfilter.nf_conntrack_acct=1")
        netns("tm_router", "nft", "add", "table", "inet", "tm_filter")
        netns("tm_router", "nft", "add", "chain", "inet", "tm_filter", "forward",
              "{ type filter hook forward priority filter; policy accept; }")
        netns("tm_router", "nft", "add", "rule", "inet", "tm_filter", "forward",
              "ct", "state", "new,established,related", "accept")
        netns("tm_router", "nft", "add", "table", "ip", "tm_nat")
        netns("tm_router", "nft", "add", "chain", "ip", "tm_nat", "post",
              "{ type nat hook postrouting priority srcnat; policy accept; }")
        for subnet in ("192.168.50.0/24", "192.168.51.0/24"):
            netns("tm_router", "nft", "add", "rule", "ip", "tm_nat", "post",
                  "oifname", "tm_rw", "ip", "saddr", subnet, "masquerade")
        server_code = (
            "import socket; s=socket.socket(__FAMILY__); "
            "s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); "
            "s.bind((__ADDR__,__PORT__)); s.listen(5)\n"
            "for _ in range(__COUNT__):\n"
            " c,a=s.accept(); c.recv(4096); "
            "c.sendall(b'HTTP/1.0 200 OK\\r\\nContent-Length: 4\\r\\n\\r\\ntest'); "
            "c.close()\n"
            "s.close()"
        )
        server4 = subprocess.Popen(
            ["ip", "netns", "exec", "tm_server", sys.executable, "-c",
             server_code.replace("__FAMILY__", "socket.AF_INET")
             .replace("__ADDR__", repr("198.51.100.2")).replace("__PORT__", "8080")
             .replace("__COUNT__", "4")],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, text=True,
        )
        server6 = subprocess.Popen(
            ["ip", "netns", "exec", "tm_server", sys.executable, "-c",
             server_code.replace("__FAMILY__", "socket.AF_INET6")
             .replace("__ADDR__", repr("fd51::2")).replace("__PORT__", "8081")
             .replace("__COUNT__", "6")],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, text=True,
        )
        processes.extend((server4, server6))
        time.sleep(0.5)
        monitor = subprocess.Popen(
            ["ip", "netns", "exec", "tm_router", str(daemon), "--database-dir",
             str(workdir / "database"), "--socket", str(socket_path), "--date", "2026-09-19",
             "--protocol-db", str(protocols), "--local-subnet", "192.168.50.0/24",
             "--local-subnet", "192.168.51.0/24", "--local-subnet", "fd50::/64",
             "--local-subnet", "fd52::/64", "--local-interface", "tm_rl",
             "--local-interface", "tm_rl2", "--refresh-seconds", "1"],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
        )
        processes.append(monitor)
        for _ in range(100):
            if socket_path.exists():
                break
            if monitor.poll() is not None:
                raise AssertionError(monitor.stderr.read())
            time.sleep(0.05)
        client_code = (
            "import socket; "
            "s=socket.socket(FAMILY,socket.SOCK_STREAM); s.settimeout(5); "
            "s.bind((SRC,0)); s.connect((ADDR,PORT)); "
            "s.sendall(b'GET / HTTP/1.0\\r\\nHost: test\\r\\n\\r\\n'); "
            "d=s.recv(65536); assert d; s.close()"
        )
        flows = (("tm_client", "socket.AF_INET", "192.168.50.2", "198.51.100.2", 8080),
                 ("tm_client", "socket.AF_INET6", "fd50::2", "fd51::2", 8081),
                 ("tm_client", "socket.AF_INET6", "fd50::22", "fd51::2", 8081),
                 ("tm_client2", "socket.AF_INET", "192.168.51.2", "198.51.100.2", 8080),
                 ("tm_client2", "socket.AF_INET6", "fd52::2", "fd51::2", 8081))
        for namespace, family, source, address, port in flows:
            code = (client_code.replace("FAMILY", family).replace("SRC", repr(source))
                    .replace("ADDR", repr(address)).replace("PORT", str(port)))
            netns(namespace, sys.executable, "-c", code)
        time.sleep(7)
        for namespace, family, source, address, port in flows:
            code = (client_code.replace("FAMILY", family).replace("SRC", repr(source))
                    .replace("ADDR", repr(address)).replace("PORT", str(port)))
            netns(namespace, sys.executable, "-c", code)
        time.sleep(2)
        result = run(str(cli), "--socket", str(socket_path), "json", "--group", "family",
                     "--group", "ip", "--group", "protocol", capture=True)
        records = json.loads(result.stdout)["records"]
        families = {row["family"] for row in records if row["protocol"] == "HTTP"}
        if families != {4, 6} or any(row["download_bytes"] == 0 or row["upload_bytes"] == 0
                                    for row in records if row["protocol"] == "HTTP"):
            raise AssertionError(result.stdout)
        raw = json.loads(run(str(cli), "--socket", str(socket_path), "json",
                             capture=True).stdout)["records"]
        if {row["family"] for row in raw if row["mac"] is not None} != {4, 6}:
            raise AssertionError("collector did not persist IPv4/IPv6 MACs: " + str(raw))
        status = json.loads(run(str(cli), "--socket", str(socket_path), "status",
                                capture=True).stdout)
        if status["netlink_parse_errors"] != 0 or status["conntrack_dump_count"] < 1:
            raise AssertionError(status)
        devices = json.loads(run(str(cli), "--socket", str(socket_path), "json",
                                 "--group", "device", capture=True).stdout)["devices"]
        known = [row for row in devices if row["mac"] is not None]
        if len(known) != 2 or not any(
            {"192.168.50.2", "fd50::2", "fd50::22"}.issubset(set(row["addresses"]))
            for row in known
        ) or not any(
            {"192.168.51.2", "fd52::2"}.issubset(set(row["addresses"]))
            for row in known
        ):
            raise AssertionError(devices)
        monitor.send_signal(signal.SIGTERM)
        if monitor.wait(timeout=10) != 0 or socket_path.exists():
            raise AssertionError("collector daemon did not stop cleanly")
        print("PASS two-MAC IPv4/IPv6 conntrack and IPv6 privacy-address device grouping")
        return 0
    finally:
        cleanup(processes)


if __name__ == "__main__":
    raise SystemExit(main())
