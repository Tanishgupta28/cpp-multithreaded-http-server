"""Exercise the real server process with a TCP client (no third-party packages)."""

import socket
import struct
import subprocess
import sys
import time


def check_connection(executable, port, arguments, message=b"GET / HTTP/1.1\r\n",
                     reset=False, error=None, fields=(b"GET", b"/", b"HTTP/1.1"),
                     fragments=None):
    process = subprocess.Popen(
        [executable, *arguments], stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
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
                expected = b"Message received.\n" if message and not error else b""
                assert response == expected, response
        output, errors = process.communicate(timeout=5)
        if reset:
            assert process.returncode == 1, (process.returncode, errors)
            assert b"recv:" in errors, errors
            return
        if error:
            assert process.returncode == 1, (process.returncode, errors)
            assert error in errors, errors
            assert b"Method:" not in output, output
            return
        assert process.returncode == 0, errors
        assert f"Listening on 127.0.0.1:{port}".encode() in output, output
        assert b"Client connected." in output, output
        assert b"Sockets closed. Server exiting." in output, output
        if message:
            method, path, version = fields
            assert b"Method: " + method + b"\n" in output, output
            assert b"Path: " + path + b"\n" in output, output
            assert b"Version: " + version + b"\n" in output, output
            assert b"Sent 18 response bytes." in output, output
        else:
            assert b"Client closed the connection without sending data." in output, output
        assert not errors, errors
    finally:
        if process.poll() is None:
            process.kill()
        process.communicate()


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
print("PASS: default port, received data, response, clean EOF and exit")
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
                     fields=(b"GET", path, version))
print("PASS: paths, HTTP/1.0 and HTTP/1.1, exact 1024-byte line")
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
print("PASS: malformed, incomplete, overlong lines and EOF without data")
check_connection(executable, custom_port, [str(custom_port)], reset=True)
print("PASS: connection reset reported without signal termination")
