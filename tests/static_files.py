"""Static-file integration tests using an isolated document root."""
import concurrent.futures
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time

executable = str(Path(sys.argv[1]).resolve())
examples = Path(__file__).resolve().parents[1] / "public"
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    public = root / "public"
    shutil.copytree(examples, public)
    binary = bytes(range(256)) * 80
    (public / "data.bin").write_bytes(binary)
    (public / "empty.txt").write_bytes(b"")
    (root / "secret.txt").write_text("outside document root")
    (public / "link.txt").symlink_to(root / "secret.txt")
    (public / "directory").mkdir()
    import os
    os.mkfifo(public / "pipe")
    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        port = reserve.getsockname()[1]
    with tempfile.TemporaryFile() as log:
        process = subprocess.Popen([executable, str(port)], cwd=root, stdout=log, stderr=log)
        try:
            deadline = time.monotonic() + 5
            while True:
                try:
                    with socket.create_connection(("127.0.0.1", port), 2):
                        break
                except ConnectionRefusedError:
                    assert process.poll() is None and time.monotonic() < deadline
                    time.sleep(0.02)

            def check(path, status, body, mime=b"text/plain"):
                with socket.create_connection(("127.0.0.1", port), 3) as client:
                    client.sendall(b"GET " + path + b" HTTP/1.1\r\n")
                    response = b""
                    while True:
                        chunk = client.recv(4096)
                        if not chunk:
                            break
                        response += chunk
                headers, separator, actual = response.partition(b"\r\n\r\n")
                assert separator and actual == body, path
                assert headers.split(b"\r\n") == [
                    b"HTTP/1.1 " + status,
                    b"Content-Type: " + mime,
                    b"Content-Length: " + str(len(body)).encode(),
                    b"Connection: close",
                ], (path, headers)

            cases = [
                (b"/", b"200 OK", b"Hello from C++ HTTP server!\n", b"text/plain"),
                (b"/health", b"200 OK", b"OK\n", b"text/plain"),
                (b"/index.html", b"200 OK", (public / "index.html").read_bytes(), b"text/html"),
                (b"/style.css", b"200 OK", (public / "style.css").read_bytes(), b"text/css"),
                (b"/hello.txt", b"200 OK", (public / "hello.txt").read_bytes(), b"text/plain"),
                (b"/data.bin", b"200 OK", binary, b"application/octet-stream"),
                (b"/empty.txt", b"200 OK", b"", b"text/plain"),
            ]
            for case in cases:
                check(*case)
            for path in (b"/missing.txt", b"/../secret.txt", b"/../../etc/passwd",
                         b"//etc/passwd", b"/%2e%2e/secret.txt", b"/..%2fsecret.txt",
                         b"/..\\secret.txt", b"/link.txt", b"/directory", b"/pipe",
                         b"/.hidden", b"/hello.txt?x=1", b"/index.html/child"):
                check(path, b"404 Not Found", b"Not Found\n")
            # More concurrent clients than workers, with different file sizes/types.
            with concurrent.futures.ThreadPoolExecutor(max_workers=12) as clients:
                futures = [clients.submit(check, *case) for case in cases * 6]
                for future in futures:
                    future.result()
            # Every request reopens the file; no caching is introduced.
            (public / "hello.txt").write_bytes(b"Updated\n")
            check(b"/hello.txt", b"200 OK", b"Updated\n")
            public.rename(root / "saved-public")
            check(b"/hello.txt", b"404 Not Found", b"Not Found\n")
            public.symlink_to(root / "saved-public", target_is_directory=True)
            check(b"/hello.txt", b"404 Not Found", b"Not Found\n")
            assert process.poll() is None
        finally:
            process.kill()
            process.wait(timeout=5)
print("PASS: static MIME/bytes, binary/empty files, traversal/symlinks, 42 concurrent requests, missing root")
