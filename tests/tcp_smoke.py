"""Exercise the real server process with a TCP client (no third-party packages)."""

import os
import select
import socket
import struct
import subprocess
import sys
import time
import tempfile
from pathlib import Path


def check_response(response, bad_request=False, status=b"200 OK",
                   expected_body=b"Hello from C++ HTTP server!\n"):
    headers, separator, body = response.partition(b"\r\n\r\n")
    assert separator == b"\r\n\r\n", response
    lines = headers.split(b"\r\n")
    if bad_request:
        status, expected_body = b"400 Bad Request", b"Bad Request\n"
    assert lines[0] == b"HTTP/1.1 " + status, lines[0]
    assert len(lines) == 4, lines
    assert lines[1] == b"Content-Type: text/plain", lines
    assert lines[2] == b"Content-Length: " + str(len(body)).encode(), lines
    assert lines[3] == b"Connection: close", lines
    assert body == expected_body, body


def check_connection(executable, port, arguments, message=b"GET / HTTP/1.1\r\n",
                     reset=False, error=None, fields=(b"GET", b"/", b"HTTP/1.1"),
                     fragments=None, status=b"200 OK",
                     expected_body=b"Hello from C++ HTTP server!\n"):
    output_file = tempfile.TemporaryFile()
    error_file = tempfile.TemporaryFile()
    process = subprocess.Popen(
        [executable, *arguments], stdout=output_file, stderr=error_file,
    )
    try:
        deadline = time.monotonic() + 5
        while True:
            try:
                client = socket.create_connection(("127.0.0.1", port), timeout=1)
                break
            except ConnectionRefusedError:
                if process.poll() is not None or time.monotonic() >= deadline:
                    raise AssertionError("Server did not start accepting connections")
                time.sleep(0.02)
        with client:
            if reset:
                # Closing with zero linger sends a TCP reset instead of orderly EOF.
                client.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                  struct.pack("ii", 1, 0))
            else:
                if fragments:
                    for fragment in fragments:
                        client.sendall(fragment)
                        time.sleep(0.03)
                elif message:
                    client.sendall(message)
                client.shutdown(socket.SHUT_WR)
                response = b""
                while True:
                    chunk = client.recv(1024)
                    if not chunk:
                        break
                    response += chunk
                if message:
                    check_response(response, bad_request=bool(error),
                                   status=status, expected_body=expected_body)
                else:
                    assert response == b"", response
        deadline = time.monotonic() + 5
        while True:
            output = os.pread(output_file.fileno(), 65536, 0)
            if b"Client socket closed." in output:
                break
            assert process.poll() is None, "Server exited after a client"
            assert time.monotonic() < deadline, "Worker did not finish"
            time.sleep(0.01)
        assert process.poll() is None, "Server must keep accepting clients"
        errors = os.pread(error_file.fileno(), 65536, 0)
        if reset:
            assert b"recv:" in errors, errors
            return
        if error:
            assert error in errors, errors
            assert b"Method:" not in output, output
            return
        assert f"Listening on 127.0.0.1:{port}".encode() in output, output
        assert b"Client connected." in output, output
        assert b"Client socket closed." in output, output
        if message:
            method, path, version = fields
            assert b"Method: " + method + b"\n" in output, output
            assert b"Path: " + path + b"\n" in output, output
            assert b"Version: " + version + b"\n" in output, output
            assert f"Sent {len(response)} response bytes.".encode() in output, output
        else:
            assert b"Client closed the connection without sending data." in output, output
        assert not errors, errors
    finally:
        if process.poll() is None:
            process.kill()
        process.wait(timeout=5)
        output_file.close()
        error_file.close()


executable = sys.argv[1]
for arguments in (["0"], ["65536"], ["-1"], ["abc"], ["8080x"], [""],
                  ["99999999999999999999"], ["8080", "extra"]):
    result = subprocess.run([executable, *arguments], capture_output=True,
                            text=True, timeout=5)
    assert result.returncode != 0 and result.stderr, arguments
print("PASS: invalid arguments rejected")

# Reserve a port to make the bind failure deterministic.
with socket.socket() as occupied:
    occupied.bind(("127.0.0.1", 0))
    occupied.listen(1)
    port = occupied.getsockname()[1]
    result = subprocess.run([executable, str(port)], capture_output=True,
                            text=True, timeout=5)
    assert result.returncode != 0 and "bind:" in result.stderr, result
print("PASS: occupied port reported")

check_connection(executable, 8080, [])
print("PASS: 200 status, CRLF headers, Content-Length, body, clean EOF and continued service")
with socket.socket() as temporary:
    temporary.bind(("127.0.0.1", 0))
    custom_port = temporary.getsockname()[1]
# There is a small allocation race after releasing this ephemeral port.
check_connection(executable, custom_port, [str(custom_port)])
check_connection(executable, custom_port, [str(custom_port)])
print("PASS: custom port and immediate restart")
for path, version in ((b"/index.html", b"HTTP/1.1"), (b"/", b"HTTP/1.0"),
                      (b"/search?q=test", b"HTTP/1.1"),
                      (b"/" + b"x" * 1008, b"HTTP/1.1")):
    message = b"GET " + path + b" " + version + b"\r\n"
    check_connection(executable, custom_port, [str(custom_port)], message,
                     fields=(b"GET", path, version),
                     status=b"200 OK" if path == b"/" else b"404 Not Found",
                     expected_body=b"Hello from C++ HTTP server!\n" if path == b"/" else b"Not Found\n")
print("PASS: paths, HTTP/1.0 and HTTP/1.1, exact 1024-byte line")
# Explicit cases keep expected responses independent of the router implementation.
route_cases = [
    (b"GET / HTTP/1.1\r\n", b"200 OK", b"Hello from C++ HTTP server!\n"),
    (b"GET /health HTTP/1.1\r\n", b"200 OK", b"OK\n"),
    (b"GET /missing HTTP/1.1\r\n", b"404 Not Found", b"Not Found\n"),
    (b"POST /health HTTP/1.1\r\n", b"404 Not Found", b"Not Found\n"),
    (b"GET / WRONG\r\n", b"400 Bad Request", b"Bad Request\n"),
]
for message, status, body in route_cases[:4]:
    check_connection(executable, custom_port, [str(custom_port)], message,
                     fields=tuple(message.strip().split(b" ")), status=status,
                     expected_body=body)
for method, path in ((b"get", b"/"), (b"GET", b"/Health"),
                     (b"GET", b"/health/"), (b"GET", b"/health?check=1"),
                     (b"PUT", b"/missing")):
    check_connection(executable, custom_port, [str(custom_port)],
                     method + b" " + path + b" HTTP/1.1\r\n",
                     fields=(method, path, b"HTTP/1.1"),
                     status=b"404 Not Found", expected_body=b"Not Found\n")
print("PASS: home, health, missing routes, unsupported methods and exact matching")

check_connection(executable, custom_port, [str(custom_port)],
                 b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
check_connection(executable, custom_port, [str(custom_port)],
                 fragments=[b"G", b"ET / HT", b"TP/1.1\r", b"\n"])
print("PASS: trailing headers ignored, fragmented line and split CRLF")
for message in (b"\r\n", b"GET\r\n", b"GET /\r\n", b"GET  / HTTP/1.1\r\n",
                b" GET / HTTP/1.1\r\n", b"GET / HTTP/1.1 extra\r\n",
                b"GET / HTTP/1.1 \r\n", b"GET / HTTP/2.0\r\n",
                b"GET / HTTP/1.1\n", b"GET / HTTP/1.1\rX\n",
                b"GET relative HTTP/1.1\r\n", b"GE(T / HTTP/1.1\r\n",
                b"GET\t/ HTTP/1.1\r\n", b"GET /a\x00b HTTP/1.1\r\n",
                b"GET /a\tb HTTP/1.1\r\n", b"GET /\xff HTTP/1.1\r\n"):
    check_connection(executable, custom_port, [str(custom_port)], message,
                     error=b"Malformed request line.")
for message in (b"G", b"GET / HTTP/1.1", b"GET / HTTP/1.1\r"):
    check_connection(executable, custom_port, [str(custom_port)], message,
                     error=b"Incomplete request line:")
check_connection(executable, custom_port, [str(custom_port)], b"x" * 1024,
                 error=b"Request line too long:")
check_connection(executable, custom_port, [str(custom_port)], b"")
print("PASS: 400 responses for malformed/incomplete/overlong lines; empty EOF")
check_connection(executable, custom_port, [str(custom_port)], reset=True)
print("PASS: connection reset reported without signal termination")


def read_response(client):
    response = b""
    while True:
        chunk = client.recv(1024)
        if not chunk:
            return response
        response += chunk


def check_concurrency():
    # Keep two incomplete requests open while other clients finish. A sequential
    # server cannot pass: the slow clients are completed only after the fast ones.
    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen([executable, str(custom_port)], stdout=log, stderr=log)
        clients = []
        try:
            deadline = time.monotonic() + 5
            while True:
                try:
                    clients.append(socket.create_connection(("127.0.0.1", custom_port), 2))
                    break
                except ConnectionRefusedError:
                    assert process.poll() is None, "Server exited during startup"
                    assert time.monotonic() < deadline, "Server did not start"
                    time.sleep(0.02)
            clients.append(socket.create_connection(("127.0.0.1", custom_port), 2))
            clients[0].sendall(b"GET /")
            clients[1].sendall(b"GET / HTTP/1.1\r")
            # A mix of valid and malformed overlapping connections also checks
            # that one client's parse error cannot affect another client's buffer.
            for index in range(6):
                client = socket.create_connection(("127.0.0.1", custom_port), 2)
                clients.append(client)
                client.sendall(route_cases[index % len(route_cases)][0])
            for index, client in enumerate(clients[2:]):
                _, status, body = route_cases[index % len(route_cases)]
                check_response(read_response(client), status=status, expected_body=body)
            clients[0].sendall(b" HTTP/1.1\r\n")
            clients[1].sendall(b"\n")
            for client in clients[:2]:
                check_response(read_response(client), bad_request=False)
            # The same listener must remain usable after all workers finish.
            with socket.create_connection(("127.0.0.1", custom_port), 2) as client:
                client.sendall(b"GET / HTTP/1.1\r\n")
                check_response(read_response(client), bad_request=False)
            assert process.poll() is None, "Client errors stopped the server"
        finally:
            for client in clients:
                client.close()
            process.kill()
            process.wait(timeout=5)


check_concurrency()
print("PASS: overlapping clients, two slow clients, mixed routes and 200/400/404, continued service")


def check_fixed_pool():
    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen([executable, str(custom_port)], stdout=log, stderr=log)
        clients = []

        def thread_ids():
            # Linux /proc includes the main thread as well as the four workers.
            return set(os.listdir(f"/proc/{process.pid}/task"))

        def wait_for_log(marker, count=1):
            deadline = time.monotonic() + 5
            while os.pread(log.fileno(), 65536, 0).count(marker) < count:
                assert process.poll() is None, "Pool server exited"
                assert time.monotonic() < deadline, "Pool did not become ready"
                time.sleep(0.01)

        def check_idle_workers(original_threads):
            worker_ids = original_threads - {str(process.pid)}
            def states():
                return [Path(f"/proc/{process.pid}/task/{tid}/wchan").read_text()
                        for tid in worker_ids]
            deadline = time.monotonic() + 5
            while not all("futex" in state for state in states()):
                assert time.monotonic() < deadline, states()
                time.sleep(0.01)
            def cpu_ticks():
                total = 0
                for tid in worker_ids:
                    fields = Path(f"/proc/{process.pid}/task/{tid}/stat").read_text().rsplit(")", 1)[1].split()
                    total += int(fields[11]) + int(fields[12])
                return total
            before = cpu_ticks()
            time.sleep(0.2)
            assert cpu_ticks() - before <= 2, "Idle workers consumed CPU instead of sleeping"
            assert all("futex" in state for state in states())

        try:
            wait_for_log(b"4 reusable workers")
            original_threads = thread_ids()
            assert len(original_threads) == 5, original_threads
            check_idle_workers(original_threads)
            for index in range(4):
                client = socket.create_connection(("127.0.0.1", custom_port), 2)
                clients.append(client)
                client.sendall(b"GET /")
                wait_for_log(b"Client connected.", index + 1)
            assert thread_ids() == original_threads

            # All workers are occupied; the producer must still accept and enqueue.
            for index in range(8):
                client = socket.create_connection(("127.0.0.1", custom_port), 2)
                clients.append(client)
                client.sendall(b"GET / HTTP/1.1\r\n")
                assert thread_ids() == original_threads
            wait_for_log(b"Client connected.", 12)
            # The server now owns 12 accepted sockets plus its listener. Stage 7
            # had only four accepted sockets here; the remainder stayed in the kernel.
            server_sockets = [name for name in os.listdir(f"/proc/{process.pid}/fd")
                              if os.readlink(f"/proc/{process.pid}/fd/{name}").startswith("socket:")]
            assert len(server_sockets) == 13, server_sockets
            ready, _, _ = select.select(clients[4:], [], [], 0.15)
            assert not ready, "A response arrived while all four workers were blocked"
            assert thread_ids() == original_threads

            # Free just one worker: it must reuse itself for all eight pending clients.
            clients[0].sendall(b" HTTP/1.1\r\n")
            check_response(read_response(clients[0]), bad_request=False)
            for client in clients[4:]:
                check_response(read_response(client), bad_request=False)
                assert thread_ids() == original_threads
            for client in clients[1:4]:
                client.sendall(b" HTTP/1.1\r\n")
                check_response(read_response(client), bad_request=False)
            wait_for_log(b"Client socket closed.", 12)
            assert thread_ids() == original_threads, "Workers were replaced instead of reused"
            check_idle_workers(original_threads)
            for wave in range(3):
                wave_clients = []
                for index in range(32):
                    client = socket.create_connection(("127.0.0.1", custom_port), 2)
                    clients.append(client)
                    wave_clients.append(client)
                    client.sendall(route_cases[index % len(route_cases)][0])
                for index, client in enumerate(wave_clients):
                    _, status, body = route_cases[index % len(route_cases)]
                    check_response(read_response(client), status=status, expected_body=body)
                    client.close()
                assert thread_ids() == original_threads
                check_idle_workers(original_threads)
            assert process.poll() is None
        finally:
            for client in clients:
                client.close()
            process.kill()
            process.wait(timeout=5)


check_fixed_pool()
print("PASS: application queue, four fixed workers, idle sleep/wake, 12 queued/active clients and 96 stress requests")
