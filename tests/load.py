"""Bounded HTTP request-line load generator; Python standard library only."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import ipaddress
import json
import math
import socket
import time


def percentile(ordered, fraction):
    """Nearest rank: sorted sample at ceil(fraction * count), using 1-based ranks."""
    return ordered[max(0, math.ceil(fraction * len(ordered)) - 1)]


def request(host, port, path, timeout):
    deadline = time.perf_counter() + timeout
    with socket.create_connection((host, port), timeout=timeout) as client:
        client.settimeout(max(0.001, deadline - time.perf_counter()))
        client.sendall(f"GET {path} HTTP/1.1\r\n".encode("ascii"))
        data = bytearray()
        while True:
            remaining = deadline - time.perf_counter()
            if remaining <= 0:
                raise TimeoutError("request deadline exceeded")
            client.settimeout(remaining)
            chunk = client.recv(65536)
            if not chunk:
                break
            data.extend(chunk)
            if len(data) > 16 * 1024 * 1024:
                raise ValueError("response exceeds 16 MiB load-client limit")
    headers, separator, body = bytes(data).partition(b"\r\n\r\n")
    if not separator:
        raise ValueError("missing CRLF header/body separator")
    lines = headers.split(b"\r\n")
    if lines[0] != b"HTTP/1.1 200 OK":
        raise ValueError(f"unexpected status: {lines[0]!r}")
    fields = dict(line.split(b": ", 1) for line in lines[1:])
    if int(fields.get(b"Content-Length", b"-1")) != len(body):
        raise ValueError("incorrect Content-Length")
    if not fields.get(b"Content-Type") or fields.get(b"Connection") != b"close":
        raise ValueError("missing Content-Type or Connection: close")


def run(host, port, paths, requests, concurrency, timeout):
    def worker(offset):
        latencies, errors = [], []
        failed = 0
        for index in range(offset, requests, concurrency):
            start = time.perf_counter()
            try:
                request(host, port, paths[index % len(paths)], timeout)
            except (OSError, ValueError) as error:
                failed += 1
                if len(errors) < 3:
                    errors.append(str(error))
            latencies.append((time.perf_counter() - start) * 1000)
        return latencies, failed, errors

    start = time.perf_counter()
    with ThreadPoolExecutor(max_workers=concurrency) as pool:
        results = list(pool.map(worker, range(concurrency)))
    elapsed = time.perf_counter() - start
    latencies = sorted(value for result in results for value in result[0])
    failed = sum(result[1] for result in results)
    return {
        "host": host, "port": port, "paths": paths, "requests": requests,
        "concurrency": concurrency, "timeout_seconds": timeout,
        "successful": requests - failed, "failed": failed,
        "elapsed_seconds": elapsed,
        "requests_per_second": (requests - failed) / elapsed,
        "latency_ms": {"average": sum(latencies) / len(latencies),
                       "p50": percentile(latencies, 0.50),
                       "p95": percentile(latencies, 0.95)},
        "error_samples": [error for result in results for error in result[2]][:5],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1", help="IPv4 address (no DNS)")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--requests", type=int, default=1000)
    parser.add_argument("--concurrency", type=int, default=4)
    parser.add_argument("--path", action="append", help="repeat for round-robin mixed routes")
    parser.add_argument("--timeout", type=float, default=3)
    args = parser.parse_args()
    try:
        ipaddress.IPv4Address(args.host)
    except ipaddress.AddressValueError:
        parser.error("host must be an IPv4 address")
    if not (1 <= args.requests <= 100000 and 1 <= args.concurrency <= 128
            and 1 <= args.port <= 65535 and 0 < args.timeout <= 60):
        parser.error("requests: 1..100000, concurrency: 1..128, port: 1..65535, timeout: (0,60]")
    paths = args.path or ["/health"]
    if any(not path.startswith("/") or any(ord(c) < 33 or ord(c) > 126 for c in path)
           or len(path) > 1000 for path in paths):
        parser.error("paths must be slash-prefixed visible ASCII without spaces, <=1000 characters")
    result = run(args.host, args.port, paths, args.requests, args.concurrency, args.timeout)
    print(json.dumps(result, indent=2))
    return int(result["failed"] != 0)


if __name__ == "__main__":
    raise SystemExit(main())
