#!/usr/bin/env python3
"""Linux SIGTERM/SIGINT persistence and socket-cleanup integration test."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import signal
import subprocess
import time
from pathlib import Path


def cli(path: Path, cwd: Path, *args: str) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(
        [str(path), "--socket", "trafficmon.sock", *args], cwd=cwd, text=True,
        capture_output=True, timeout=10, check=False
    )
    if result.returncode != 0:
        raise AssertionError(f"CLI failed ({result.returncode}): {result.stderr}")
    return result


def start(daemon: Path, cli_path: Path, cwd: Path) -> subprocess.Popen[str]:
    process = subprocess.Popen(
        [str(daemon), "--database-dir", "database", "--socket", "trafficmon.sock",
         "--date", "2026-09-19"],
        cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if process.poll() is not None:
            out, err = process.communicate()
            raise AssertionError(f"daemon exited early ({process.returncode}): {out} {err}")
        probe = subprocess.run(
            [str(cli_path), "--socket", "trafficmon.sock", "status"], cwd=cwd,
            text=True, capture_output=True, check=False
        )
        if probe.returncode == 0:
            return process
        time.sleep(0.05)
    process.kill()
    raise AssertionError("daemon did not become ready")


def stop_with_signal(process: subprocess.Popen[str], sig: signal.Signals,
                     socket_path: Path) -> None:
    os.kill(process.pid, sig)
    process.wait(timeout=10)
    if process.returncode != 0:
        out, err = process.communicate()
        raise AssertionError(f"daemon signal exit was {process.returncode}: {out} {err}")
    if socket_path.exists():
        raise AssertionError("daemon left its AF_UNIX socket path behind")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--daemon", required=True, type=Path)
    parser.add_argument("--cli", required=True, type=Path)
    parser.add_argument("--workdir", required=True, type=Path)
    args = parser.parse_args()
    daemon = args.daemon.resolve()
    cli_path = args.cli.resolve()
    workdir = args.workdir.resolve()
    if workdir.exists():
        shutil.rmtree(workdir)
    (workdir / "database").mkdir(parents=True)

    process = start(daemon, cli_path, workdir)
    cli(cli_path, workdir, "__test_seed")
    stop_with_signal(process, signal.SIGTERM, workdir / "trafficmon.sock")

    process = start(daemon, cli_path, workdir)
    status = json.loads(cli(cli_path, workdir, "status").stdout)
    if status["record_count"] != 2:
        raise AssertionError("SIGTERM commit was not restored")
    stop_with_signal(process, signal.SIGINT, workdir / "trafficmon.sock")
    print("PASS SIGTERM/SIGINT clean exit, socket cleanup, and persistence")


if __name__ == "__main__":
    main()
