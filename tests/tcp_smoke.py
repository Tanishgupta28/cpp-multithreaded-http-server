"""Exercise the real server process with a TCP client (no third-party packages)."""

import socket
import struct
import subprocess
import sys
import time


def check_connection(executable, port, arguments, message=b"Hello, server!", reset=False):
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
                if message:
                    client.sendall(message)
                client.shutdown(socket.SHUT_WR)
                response = b""
                while True:
                    chunk = client.recv(1024)
                    if not chunk:
                        break
                    response += chunk
                expected = b"Message received.\n" if message else b""
                assert response == expected, response
        output, errors = process.communicate(timeout=5)
        if reset:
            assert process.returncode == 1, (process.returncode, errors)
            assert b"recv:" in errors, errors
            return
        assert process.returncode == 0, errors
        assert f"Listening on 127.0.0.1:{port}".encode() in output, output
        assert b"Client connected." in output, output
        assert b"Sockets closed. Server exiting." in output, output
        if message:
            # Even a single sendall can be split across TCP receives.
            received_log = output.split(b"Received ", 1)[1]
            count_text, data_log = received_log.split(b" bytes: [", 1)
            count = int(count_text)
            assert 0 < count <= min(len(message), 1024), output
            assert data_log.startswith(message[:count] + b"]\n"), output
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
for message in (b"x", b"before\x00after", b"x" * 1024, b""):
    check_connection(executable, custom_port, [str(custom_port)], message)
print("PASS: one byte, embedded NUL, full buffer, orderly EOF without data")
check_connection(executable, custom_port, [str(custom_port)], reset=True)
print("PASS: connection reset reported without signal termination")
