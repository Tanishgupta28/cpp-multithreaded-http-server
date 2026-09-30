"""SIGINT shutdown, draining, thread joining, and complete log-line checks."""
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time

executable = str(Path(sys.argv[1]).resolve())
project = Path(__file__).resolve().parents[1]
with socket.socket() as reserve:
    reserve.bind(("127.0.0.1", 0))
    port = reserve.getsockname()[1]

for drain in (False, True, False):
    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen([executable, str(port)], cwd=project,
                                   stdout=log, stderr=log)
        clients = []
        def output():
            return os.pread(log.fileno(), 100000, 0)
        def wait_for(marker, count=1):
            deadline = time.monotonic() + 5
            while output().count(marker) < count:
                assert process.poll() is None, output()
                assert time.monotonic() < deadline, output()
                time.sleep(0.01)
        try:
            wait_for(b"Listening on")
            assert len(os.listdir(f"/proc/{process.pid}/task")) == 5
            if drain:
                for _ in range(4):
                    client = socket.create_connection(("127.0.0.1", port), 3)
                    clients.append(client)
                    client.sendall(b"GET /")
                queued = [b"GET /health HTTP/1.1\r\n", b"GET /hello.txt HTTP/1.1\r\n",
                          b"GET / WRONG\r\n", b"GET /absent.txt HTTP/1.1\r\n"] * 2
                for request in queued:
                    client = socket.create_connection(("127.0.0.1", port), 3)
                    clients.append(client)
                    client.sendall(request)
                wait_for(b"Client connected.", 12)
            process.send_signal(signal.SIGINT)
            wait_for(b"queue closed.") if drain else None
            if drain:
                assert process.poll() is None, "Slow clients should be allowed to finish"
                try:
                    with socket.create_connection(("127.0.0.1", port), 0.5):
                        raise AssertionError("Listener still accepting after shutdown")
                except ConnectionRefusedError:
                    pass
                process.send_signal(signal.SIGINT) # Repeated notifications are harmless.
                for client in clients[:4]:
                    client.sendall(b" HTTP/1.1\r\n")
                bodies = [b"Hello from C++ HTTP server!\n"] * 4 + [
                    b"OK\n", (project / "public/hello.txt").read_bytes(),
                    b"Bad Request\n", b"Not Found\n"] * 2
                statuses = [b"200 OK"] * 4 + [b"200 OK", b"200 OK", b"400 Bad Request", b"404 Not Found"] * 2
                for client, body, status in zip(clients, bodies, statuses):
                    response = b""
                    while True:
                        chunk = client.recv(4096)
                        if not chunk: break
                        response += chunk
                    headers, separator, actual = response.partition(b"\r\n\r\n")
                    assert separator and actual == body
                    assert headers.startswith(b"HTTP/1.1 " + status + b"\r\n")
                    assert b"Content-Length: " + str(len(body)).encode() + b"\r\n" in headers
            assert process.wait(timeout=5) == 0, output()
            text = output()
            assert text.count(b"[INFO] Worker stopped.\n") == 4, text
            assert text.endswith(b"[INFO] All workers joined; server stopped.\n"), text
            for line in text.splitlines():
                assert line.startswith((b"[INFO] ", b"[ERROR] ")), line
                assert line.count(b"[INFO]") + line.count(b"[ERROR]") == 1, line
            if drain:
                assert text.count(b"[INFO] Client socket closed.\n") == 12
                assert text.count(b"[INFO] HTTP/1.1 ") == 12
        finally:
            for client in clients: client.close()
            if process.poll() is None: process.kill()
            process.wait(timeout=5)
print("PASS: idle SIGINT, 12-client drain, repeated signals/start-stop, joined workers, complete log lines")
