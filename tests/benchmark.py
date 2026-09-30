"""Run bounded workloads against a fresh local server and check its stability."""
import argparse
import json
import os
from pathlib import Path
import platform
import signal
import socket
import subprocess
import sys
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("executable")
parser.add_argument("--requests", type=int, default=500)
args = parser.parse_args()
if not 1 <= args.requests <= 10000:
    parser.error("requests must be 1..10000 per workload")
project = Path(__file__).resolve().parents[1]
executable = str(Path(args.executable).resolve())
with socket.socket() as reserve:
    reserve.bind(("127.0.0.1", 0))
    port = reserve.getsockname()[1]

with tempfile.TemporaryFile() as log:
    server = subprocess.Popen([executable, str(port)], cwd=project, stdout=log, stderr=log)
    def threads():
        return set(os.listdir(f"/proc/{server.pid}/task"))
    def descriptors():
        return len(os.listdir(f"/proc/{server.pid}/fd"))
    results = []
    try:
        deadline = time.monotonic() + 5
        while b"Listening on" not in os.pread(log.fileno(), 4096, 0):
            assert server.poll() is None, "server failed during startup"
            assert time.monotonic() < deadline, "startup deadline exceeded"
            time.sleep(0.01)
        original_threads = threads()
        assert len(original_threads) == 5
        baseline_fds = descriptors()
        scenarios = [("cold_application_cache", 1, 1, ["/index.html"]),
                     ("warm_application_cache", 1, 1, ["/index.html"])]
        for concurrency in (1, 4, 12):
            for label, paths in (("health", ["/health"]), ("cached_static", ["/index.html"]),
                                 ("mixed", ["/", "/health", "/index.html", "/style.css", "/hello.txt"])):
                scenarios.append((label, args.requests, concurrency, paths))
        for label, count, concurrency, paths in scenarios:
            command = [sys.executable, str(project / "tests/load.py"), "--port", str(port),
                       "--requests", str(count), "--concurrency", str(concurrency)]
            for path in paths:
                command.extend(["--path", path])
            with tempfile.TemporaryFile() as output:
                client = subprocess.Popen(command, stdout=output, stderr=output)
                try:
                    deadline = time.monotonic() + 60
                    while client.poll() is None:
                        assert server.poll() is None, "server crashed during load"
                        assert threads() == original_threads, "worker threads changed"
                        assert time.monotonic() < deadline, "load process deadline exceeded"
                        time.sleep(0.01)
                    output.seek(0)
                    text = output.read().decode()
                    assert client.returncode == 0, text
                    result = json.loads(text)
                finally:
                    if client.poll() is None: client.kill()
                    client.wait(timeout=5)
            assert result["successful"] == count and result["failed"] == 0
            # EOF may arrive just before the worker's final bookkeeping finishes.
            deadline = time.monotonic() + 3
            while descriptors() != baseline_fds:
                assert time.monotonic() < deadline, "descriptors did not return to baseline"
                time.sleep(0.01)
            assert threads() == original_threads
            results.append({"workload": label, **result})
        server.send_signal(signal.SIGINT)
        assert server.wait(timeout=5) == 0, "shutdown failed"
        log.seek(0)
        logs = log.read()
        assert logs.count(b"[INFO] Worker stopped.\n") == 4
        assert b"All workers joined; server stopped." in logs
        assert logs.count(b"Client socket closed.") == sum(row["requests"] for row in results)
        for line in logs.splitlines():
            assert line.startswith(b"[INFO] ") and line.count(b"[INFO]") == 1, line
        assert logs.count(b"Static cache miss: index.html") == 1
        expected_index_hits = sum(row["requests"] for row in results
                                  if row["workload"] == "cached_static") + 1
        assert logs.count(b"Static cache hit: index.html") >= expected_index_hits
        cpu = next(line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines()
                   if line.startswith("model name"))
        print(json.dumps({
            "environment": {"kernel": platform.release(), "cpu": cpu,
                            "logical_cpus": os.cpu_count(), "python": platform.python_version(),
                            "memory_kib": Path("/proc/meminfo").read_text().splitlines()[0]},
            "server_command": [args.executable, str(port)],
            "logging": "enabled, stdout/stderr redirected to temporary file",
            "stability": {"thread_count": 5, "thread_ids_unchanged": True,
                          "idle_fd_count": baseline_fds, "idle_fds_restored_after_each_run": True,
                          "shutdown_exit_code": server.returncode,
                          "completed_clients": logs.count(b"Client socket closed."),
                          "index_cache_misses": logs.count(b"Static cache miss: index.html"),
                          "index_cache_hits": logs.count(b"Static cache hit: index.html")},
            "results": results,
        }, indent=2))
    finally:
        if server.poll() is None: server.kill()
        server.wait(timeout=5)
