"""Check metric arithmetic and explicit failure reporting without a server."""
import socket
import threading
from load import percentile, run

assert percentile(list(range(1, 21)), 0.5) == 10
assert percentile(list(range(1, 21)), 0.95) == 19
assert percentile([7], 0.95) == 7
# Bound but not listening: requests must fail promptly instead of being counted as successes.
with socket.socket() as unavailable:
    unavailable.bind(("127.0.0.1", 0))
    result = run("127.0.0.1", unavailable.getsockname()[1], ["/health"], 3, 2, 0.2)
assert result["requests"] == result["failed"] == 3
assert result["successful"] == result["requests_per_second"] == 0
assert result["error_samples"] and result["elapsed_seconds"] > 0
assert result["latency_ms"]["p95"] >= result["latency_ms"]["p50"]
# Exercise malformed replies and a peer that connects but never sends a reply.
for response in (b"HTTP/1.1 200 OK\r\nContent-Length: 4\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nx", None):
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(2)
        release = threading.Event()
        def peer():
            with listener.accept()[0] as client:
                client.settimeout(2)
                client.recv(1024)
                if response is not None:
                    client.sendall(response)
                else:
                    release.wait(2)
        worker = threading.Thread(target=peer)
        worker.start()
        try:
            result = run("127.0.0.1", listener.getsockname()[1], ["/health"], 1, 1, 0.1)
            assert result["failed"] == 1 and result["successful"] == 0
        finally:
            release.set()
            worker.join(timeout=3)
            assert not worker.is_alive()
print("PASS: nearest-rank percentiles, failed replies/connections, and request timeout")
