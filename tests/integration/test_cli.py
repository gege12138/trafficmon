#!/usr/bin/env python3
"""Rootless daemon/CLI persistence and rollover integration test."""

from __future__ import annotations

import argparse
import json
import shutil
import socket
import struct
import subprocess
import time
from pathlib import Path


def run_cli(cli: Path, cwd: Path, *arguments: str, expected: int = 0) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(cli), "--socket", "trafficmon.sock", *arguments],
        cwd=cwd,
        text=True,
        capture_output=True,
        timeout=10,
        check=False,
    )
    if result.returncode != expected:
        raise AssertionError(
            f"CLI {' '.join(arguments)} returned {result.returncode}, expected {expected}\n"
            f"stdout: {result.stdout}\nstderr: {result.stderr}"
        )
    return result


def start_daemon(daemon: Path, cwd: Path, date: str) -> subprocess.Popen[str]:
    process = subprocess.Popen(
        [
            str(daemon),
            "--database-dir",
            "database",
            "--socket",
            "trafficmon.sock",
            "--date",
            date,
            "--generations",
            "3",
        ],
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if process.poll() is not None:
            stdout, stderr = process.communicate()
            raise AssertionError(f"daemon exited early: {process.returncode}\n{stdout}\n{stderr}")
        probe = subprocess.run(
            [str(cli_path), "--socket", "trafficmon.sock", "status"],
            cwd=cwd,
            text=True,
            capture_output=True,
            check=False,
        )
        if probe.returncode == 0:
            return process
        time.sleep(0.05)
    process.kill()
    raise AssertionError("daemon socket did not become ready")


def stop_daemon(process: subprocess.Popen[str], cli: Path, cwd: Path) -> None:
    run_cli(cli, cwd, "__test_stop")
    process.wait(timeout=10)
    if process.returncode != 0:
        stdout, stderr = process.communicate()
        raise AssertionError(f"daemon stop returned {process.returncode}\n{stdout}\n{stderr}")


def receive_exact(connection: socket.socket, length: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < length:
        chunk = connection.recv(length - len(chunks))
        if not chunk:
            raise AssertionError("unexpected IPC disconnect")
        chunks.extend(chunk)
    return bytes(chunks)


def receive_frame(connection: socket.socket) -> tuple[int, int, bytes]:
    header = receive_exact(connection, 20)
    magic, version, command, flags, payload_length, request_id = struct.unpack(
        "<4sHHIII", header
    )
    assert magic == b"TMIP" and version == 1
    return command, flags, receive_exact(connection, payload_length) if payload_length else b""


def exercise_raw_ipc(cwd: Path) -> None:
    if not hasattr(socket, "AF_UNIX"):
        print("SKIP raw AF_UNIX fragmentation tests: Python runtime lacks AF_UNIX")
        return
    endpoint = str(cwd / "trafficmon.sock")
    status = struct.pack("<4sHHIII", b"TMIP", 1, 1, 0, 0, 101)
    ping = struct.pack("<4sHHIII", b"TMIP", 1, 5, 0, 0, 102)
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.connect(endpoint)
        for byte in status:
            connection.sendall(bytes([byte]))
        command, flags, payload = receive_frame(connection)
        assert command == 0x8001 and flags == 0 and b"daemon_version" in payload
        connection.sendall(ping + struct.pack("<4sHHIII", b"TMIP", 1, 0x777, 0, 0, 103))
        command, flags, payload = receive_frame(connection)
        assert command == 0x8005 and flags == 0 and payload == b"pong\n"
        command, flags, payload = receive_frame(connection)
        assert command == (0x777 | 0x8000) and flags == 3 and payload == b""

    for malformed, expected_status in (
        (struct.pack("<4sHHIII", b"TMIP", 99, 1, 0, 0, 104), 2),
        (struct.pack("<4sHHIII", b"TMIP", 1, 1, 0, 0x7FFFFFFF, 105), 4),
    ):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.connect(endpoint)
            connection.sendall(malformed)
            command, flags, payload = receive_frame(connection)
            assert command == 0x8001 and flags == expected_status and payload == b""

    for malformed in (b"BAD!" + status[4:], status[:7]):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.connect(endpoint)
            connection.sendall(malformed)
            connection.shutdown(socket.SHUT_WR)
            assert connection.recv(1) == b""

    device_query = struct.pack("<IIBB2xII", 0, 16, 0, 1, 50, 0)
    for command, body in ((6, device_query + b"x" * 65 + b"\0"),
                          (6, device_query + b"\xff\0"),
                          (6, device_query + b"a\0b\0"),
                          (8, device_query + b"ff:gg:00:00:00:00\0")):
        header = struct.pack("<4sHHIII", b"TMIP", 1, command, 0, len(body), 106)
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.connect(endpoint)
            connection.sendall(header + body)
            reply_command, reply_status, reply_payload = receive_frame(connection)
            assert reply_command == (command | 0x8000) and reply_status == 1
            assert reply_payload == b""


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--daemon", type=Path, required=True)
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--workdir", type=Path, required=True)
    args = parser.parse_args()
    global cli_path
    cli_path = args.cli.resolve()
    daemon_path = args.daemon.resolve()
    version = subprocess.run([str(daemon_path), "--version"], check=True,
                             capture_output=True, text=True)
    assert version.stdout == "trafficmon 0.3.0\n"
    cli_version = subprocess.run([str(cli_path), "--version"], check=True,
                                 capture_output=True, text=True)
    assert cli_version.stdout == "trafficctl 0.3.0\n"
    subprocess.run([str(daemon_path), "--validate-subnet", "192.0.2.0/24"], check=True)
    bad_subnet = subprocess.run(
        [str(daemon_path), "--validate-subnet", "not/a/cidr"],
        capture_output=True,
        text=True,
        check=False,
    )
    assert bad_subnet.returncode == 2 and "invalid local subnet" in bad_subnet.stderr
    subprocess.run([str(daemon_path), "--validate-period", "1"], check=True)
    bad_period = subprocess.run(
        [str(daemon_path), "--validate-period", "bogus"],
        capture_output=True,
        text=True,
        check=False,
    )
    assert bad_period.returncode == 2 and "invalid accounting period" in bad_period.stderr
    workdir = args.workdir.resolve()
    if workdir.exists():
        shutil.rmtree(workdir)
    workdir.mkdir(parents=True)
    (workdir / "database").mkdir()

    daemon = start_daemon(daemon_path, workdir, "2026-09-19")
    try:
        exercise_raw_ipc(workdir)
        run_cli(cli_path, workdir, "__test_seed")
        status = json.loads(run_cli(cli_path, workdir, "status").stdout)
        assert status["record_count"] == 2
        assert status["unresolved_addresses"] == 1
        assert status["current_period"]["start"] == "2026-09-01"
        periods = json.loads(run_cli(cli_path, workdir, "list").stdout)
        assert len(periods["periods"]) == 1
        show = run_cli(cli_path, workdir, "show").stdout
        assert "192.168.1.100" in show and "192.168.1.101" in show
        grouped = json.loads(
            run_cli(cli_path, workdir, "json", "--group", "protocol", "--order", "upload").stdout
        )
        assert {item["protocol"] for item in grouped["records"]} == {"HTTPS", "Other"}
        devices = json.loads(run_cli(cli_path, workdir, "json", "--group", "device",
                                     "--limit", "1").stdout)
        assert devices["total_count"] == 1 and len(devices["devices"]) == 1
        found = json.loads(run_cli(cli_path, workdir, "json", "--group", "device",
                                   "--search", "192.168.1.101").stdout)
        assert found["total_count"] == 0
        detail = json.loads(run_cli(cli_path, workdir, "json", "--group", "device",
                                    "--group", "protocol", "--device-id",
                                    "02:00:00:00:00:64").stdout)
        assert detail["total_count"] == 1 and detail["devices"][0]["protocol"] == "HTTPS"
        for bad_id in ("not-a-device", "ff:gg:00:00:00:00", "ip4:999.1.1.1"):
            run_cli(cli_path, workdir, "json", "--group", "device", "--device-id", bad_id,
                    expected=4)
        missing_device = json.loads(run_cli(cli_path, workdir, "json", "--group", "device",
                                            "--device-id", "02:00:00:00:00:ff").stdout)
        assert missing_device["total_count"] == 0 and missing_device["devices"] == []
        run_cli(cli_path, workdir, "json", "--group", "device", "--search", "x" * 65,
                expected=2)
        run_cli(cli_path, workdir, "json", "--group", "device", "--search", "é",
                expected=2)
        run_cli(cli_path, workdir, "json", "--group", "device", "--limit", "-1",
                expected=2)
        before_text = run_cli(cli_path, workdir, "json").stdout
        before = json.loads(before_text)
        assert before["records"][0]["download_bytes"] == 18446744073709551615
        csv = run_cli(cli_path, workdir, "csv").stdout
        assert csv.startswith("family,ip,mac,protocol,connections")
        assert "18446744073709551615" in csv
        assert run_cli(cli_path, workdir, "commit").stdout == "committed\n"
    finally:
        stop_daemon(daemon, cli_path, workdir)

    daemon = start_daemon(daemon_path, workdir, "2026-09-19")
    try:
        after_text = run_cli(cli_path, workdir, "json").stdout
        assert after_text == before_text
        run_cli(cli_path, workdir, "__test_date", "2026-10-01")
        current = json.loads(run_cli(cli_path, workdir, "json").stdout)
        assert current["period"] == "2026-10-01" and current["records"] == []
        historical_text = run_cli(
            cli_path, workdir, "json", "--period", "2026-09-01"
        ).stdout
        assert historical_text == before_text

        for date in ("2026-11-01", "2026-12-01", "2027-01-01"):
            run_cli(cli_path, workdir, "__test_seed")
            run_cli(cli_path, workdir, "__test_date", date)
        listing = json.loads(run_cli(cli_path, workdir, "list").stdout)["periods"]
        assert [item["start"] for item in listing] == [
            "2027-01-01",
            "2026-12-01",
            "2026-11-01",
            "2026-10-01",
        ]
        missing = run_cli(
            cli_path, workdir, "json", "--period", "2026-09-01", expected=5
        )
        assert "daemon returned error" in missing.stderr
    finally:
        stop_daemon(daemon, cli_path, workdir)

    run_cli(cli_path, workdir, "status", expected=3)
    run_cli(cli_path, workdir, "not-a-command", expected=2)
    print("PASS daemon/CLI restart, rollover, history, and generation cleanup")


if __name__ == "__main__":
    cli_path = Path()
    main()
