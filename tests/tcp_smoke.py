"""Exercise the real server process with a TCP client (no third-party packages)."""

import socket
import subprocess
import sys
import time


def check_connection(executable, port, arguments):
    process = subprocess.Popen(
        [executable, *arguments], stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True,
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
            assert client.recv(1) == b"", "Expected clean EOF without application data"
        output, errors = process.communicate(timeout=5)
        assert process.returncode == 0, errors
        assert f"Listening on 127.0.0.1:{port}" in output, output
        assert "Client connected." in output, output
        assert "Sockets closed. Server exiting." in output, output
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
print("PASS: default port, accepted connection, clean EOF and exit")
with socket.socket() as temporary:
    temporary.bind(("127.0.0.1", 0))
    custom_port = temporary.getsockname()[1]
# There is a small allocation race after releasing this ephemeral port.
check_connection(executable, custom_port, [str(custom_port)])
check_connection(executable, custom_port, [str(custom_port)])
print("PASS: custom port and immediate restart")
