# Multithreaded HTTP Server in C++17

A Linux/POSIX HTTP learning project built around visible socket APIs, four reusable
workers, and a synchronized producer-consumer queue. It serves built-in routes
and static files with a shared LRU cache. All 14 development stages are complete.
This is a small systems programming project, not a production HTTP implementation.

## Key features

- TCP/IP and POSIX sockets on IPv4 loopback (`127.0.0.1`).
- HTTP/1.0 and HTTP/1.1 request-line parsing across fragmented receives.
- Fixed four-worker thread pool; FIFO task queue with `std::mutex` and
  `std::condition_variable`.
- Exact routing, binary-safe static files, and traversal/symlink protection.
- Thread-safe 16-file LRU cache using `std::unordered_map` and a doubly linked list.
- HTTP/1.1 responses with byte-counted Content-Length and partial-send handling.
- SIGINT graceful shutdown through a self-pipe, queue draining, and worker joining.
- Synchronized logging, resource cleanup, correctness/concurrency tests, and
  bounded load/performance testing.

## Build and run

Use Linux or WSL with a C++17 compiler, CMake 3.16+, and Python 3 for all seven
CTest suites. Linux-specific APIs and `/proc` checks are used; native Windows is
not supported. Validated with Alpine Linux 3.22/WSL2, GCC 14.2.0, CMake 3.31.7,
and Python 3.12.14.

From the repository root inside Linux:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
./build/http_server 8080
```

The optional port defaults to 8080 (valid range 1..65535). Run from the repository
root so `public/` is available. Ctrl+C requests graceful shutdown.
For a clean rebuild, remove only the generated `build/` directory first.

In another Linux terminal:

```sh
printf 'GET / HTTP/1.1\r\n' | nc -w 2 127.0.0.1 8080
printf 'GET /health HTTP/1.1\r\n' | nc -w 2 127.0.0.1 8080
printf 'GET /hello.txt HTTP/1.1\r\n' | nc -w 2 127.0.0.1 8080
printf 'GET /missing HTTP/1.1\r\n' | nc -w 2 127.0.0.1 8080
printf 'GET / WRONG\r\n' | nc -w 2 127.0.0.1 8080
printf 'GET /../README.md HTTP/1.1\r\n' | nc -w 2 127.0.0.1 8080
```

## Architecture

```text
Clients
   |
   v
Listening socket (127.0.0.1)
   |
   v
Accepting flow (main thread)
   |
   v
ClientTaskQueue: FIFO, mutex + condition_variable
   |
   v
4 worker threads: recv()
   |
   v
HttpRequest: parse and validate
   |
   v
HttpRouter
   +---- built-in routes --------------------------+
   |                                               |
   +---- StaticFiles: validate filename             |
             |                                     |
             v                                     |
          LRUCache                                 |
             +-- hit: immutable cached bytes ------+
             |                                     |
             +-- miss: safe disk read -> cache ----+
                                                   |
                                                   v
                                              HttpResponse
                                                   |
                                                   v
                                              send() -> close()

SIGINT -> async-signal-safe handler -> self-pipe -> accepting poll()
       -> close listener -> close/drain queue -> join workers -> cleanup
```

A request follows `accept -> queue -> worker -> recv -> parse -> route ->
cache/file (for static requests) -> response -> send -> close`. TCP is a byte
stream, so receive data is accumulated up to a 1,024-byte request-line limit,
including CRLF. The worker keeps its own request, response, buffer, and send
offset. Responses always use HTTP/1.1 and `Connection: close`.

See [architecture and design decisions](docs/architecture.md) for component
responsibilities, ownership, synchronization, and interview discussion.

## HTTP behavior

| Request | Result |
| --- | --- |
| `GET /` | 200, `Hello from C++ HTTP server!\n` |
| `GET /health` | 200, `OK\n` |
| `GET /filename` | 200 with readable, permitted file bytes from `public/` |
| Missing/unsafe file or unsupported method | 404, `Not Found\n` |
| Malformed, incomplete, or overlong request line | 400, `Bad Request\n`, if the peer can receive |

Parsing requires exactly one space between method, slash-prefixed visible-ASCII
path, and HTTP/1.0 or HTTP/1.1, followed by CRLF. Method tokens are validated;
only GET has route behavior. Routes are case-sensitive and queries are not
stripped. Empty EOF gets no response. Headers/body data after the first line is
ignored; unread data on close can cause a TCP reset. No complete HTTP message
parser or HEAD semantics are claimed.

## Concurrency

The main thread produces accepted sockets; four worker consumers dequeue and
handle one connection each. The application queue is FIFO by dequeue order,
not by response completion order. Its mutex protects the queue and closed flag.
A condition variable sleeps idle workers with a closed-or-nonempty predicate,
handles spurious wakeups, and avoids busy waiting. Push wakes one worker; closure
wakes all. No client I/O runs under the queue mutex.

Client recv/send calls are blocking. Four slow clients can occupy every worker;
further accepted clients wait in the **unbounded** queue. There is no overload
response or queue limit. Queued sockets consume descriptors and memory. The
kernel listen backlog (requested size 16) holds connections not yet accepted;
it is separate from the application queue. Only the listener is nonblocking,
with `poll()` coordinating connection readiness and shutdown notification.

## LRU cache

All workers share a cache of **16 successful static-file reads**. A hit reuses
immutable body bytes and Content-Type and promotes the entry to most recently
used (MRU). A miss reads from disk and inserts at the front. Inserting beyond
capacity evicts the least recently used (LRU) entry at the back. Errors are not cached.

`std::unordered_map` maps filenames to `std::list` iterators; the doubly linked
list supports promotion with `splice`. Lookup/update/eviction bookkeeping is
average O(1) with respect to entry count. Filename hashing depends on key length;
file reads, response-body copies, and serialization depend on byte count.

A dedicated cache mutex protects map/list operations. `shared_ptr<const CachedFile>`
keeps bytes valid after unlocking or concurrent eviction. Disk reads, response
construction, logging, and network I/O occur outside the cache lock. Concurrent
misses may duplicate disk reads. Capacity counts files, not bytes; in-flight
responses can retain evicted bytes.

There is **no TTL or invalidation**. Edits, removals, replacements, and changes to
`public/` do not affect hits until eviction or restart. Keep the document root
and working directory fixed; restart to reliably refresh cached content.

## Static-file security

`public/` is the document root relative to the process working directory.
Only single-level filenames are accepted. Dot-prefixed names, `..`, extra slashes,
backslashes, percent escapes, queries, and fragments are rejected before lookup.
On misses, `open` anchors the root directory and `openat` opens the filename
relative to that descriptor. `O_NOFOLLOW` rejects root/file symlinks during open;
`fstat` accepts regular files only. Nonblocking file open prevents FIFO hangs.
RAII closes file/directory descriptors on normal and exception paths.

Reads append actual byte counts, preserving NULs and binary data. MIME types are
`text/html`, `text/css`, `text/plain`, or `application/octet-stream` by extension.
Missing, inaccessible, unsafe, and nonregular resources return generic 404s.
The root is operator-controlled; do not place sensitive files or hard links there.
Cached hits reuse previously validated bytes without reopening the file.

## Graceful shutdown and logging

Ctrl+C sends SIGINT. The handler preserves errno and writes to a nonblocking
self-pipe using async-signal-safe `write`; it never allocates, logs, or locks.
The accepting flow observes pipe readiness, stops accepting, closes the listener
and queue, and notifies waiting workers. Workers finish active clients and drain
accepted tasks, then exit. Main joins all four workers before queue destruction;
the signal helper restores the handler and closes the pipe. Normal shutdown exits 0.
A connection accepted concurrently with SIGINT may join the drain.

The Logger namespace protects complete INFO/ERROR lines with its own mutex and
flushes each line. It records startup, requests, cache hits/misses, errors, and
shutdown. Client failures are isolated; fatal startup/accepting failures return 1.
Partial sends and EINTR are handled; sends use MSG_NOSIGNAL. Close is not retried
because a descriptor could already have been reused.

## Testing

Python must be present when configuring CMake to register all seven suites.
Integration tests require Linux `/proc` (including readable worker `wchan`) and
an available port 8080 for default-port checks.

| CTest suite | Coverage |
| --- | --- |
| `lru_cache` | Hit/miss, promotion, replacement, eviction, capacities, binary data, retained ownership, concurrent access |
| `client_task_queue` | FIFO, blocking wait, push/close wakeups, closure rejection, draining |
| `tcp_smoke` | Ports/errors, HTTP bytes, fragmented/malformed lines, disconnects, fixed worker IDs, saturation, idle waits, repeated concurrent waves |
| `static_files` | MIME/length/bytes, binary and empty files, traversal/symlinks, nonregular files, cache staleness/eviction, concurrent requests |
| `shutdown` | Idle/busy/repeated SIGINT, accepted-work draining, listener closure, worker joining, restart, synchronized logs |
| `load_metrics` | Percentile arithmetic, refused connections, invalid responses, deadlines |
| `load_regression` | Short health/static/mixed workloads at 1/4/12 clients, stable workers/descriptors, shutdown |

Rare allocation/syscall failures and forced partial sends/EINTR are not
systematically fault-injected. Passing tests is evidence for exercised behavior,
not proof against all races or failures. Stage-by-stage evidence is in
[ROADMAP.md](ROADMAP.md).

Stage 13 added a Python standard-library closed-loop load client: each client
opens a connection, sends a request line, validates a 200 response, and reads
until EOF before starting another request. The runner manages a fresh server,
samples worker IDs, checks descriptor recovery and logs, and verifies SIGINT exit.
A small final load run uses:

```sh
python3 tests/benchmark.py ./build/http_server --requests 50 > build/final-load.json
```

For a running server:

```sh
python3 tests/load.py --port 8080 --requests 500 --concurrency 4 --path /health
```

Throughput is successful requests divided by elapsed time, including client
executor startup/join. Latency covers connection through response validation.
Average and nearest-rank p50/p95 (`ceil(p*n)`) include all attempts. Non-200 replies
count as load failures; expected 400/404 behavior is checked by correctness tests.
No suite requires a throughput target.

## Stage 13 benchmarks

### Recorded local run (2026-09-30)

Environment: Alpine Linux 3.22 under WSL2, kernel 5.15.167.4, Intel i5-1135G7
2.40 GHz, 8 logical CPUs visible, about 7.6 GiB RAM visible, GCC 14.2.0,
CMake 3.31.7, Python 3.12.14. CMake build type was unset (default compiler flags,
no optimization flag selected). Client and server ran on the same WSL instance
through loopback, with the project/public files on `/mnt/c`. Server logging stayed
enabled and was redirected to a temporary file. No CPU pinning or OS-cache flush.

Exact reproduction command from the project root after the documented build:

```sh
python3 tests/benchmark.py ./build/http_server --requests 500 > build/benchmark.json
```

This runner selects a free port, starts a fresh server, performs one initial
/index.html request and one repeat, then runs the three workloads at each of
1, 4, and 12 clients. It samples worker IDs during load, checks idle descriptor
counts after each workload, verifies response/close log counts, and requests
SIGINT shutdown. Each load subprocess has a 60-second deadline; startup/shutdown
have five-second deadlines. Failed runs stop their helper processes.

[Raw measured output](benchmarks/stage13.json) contains unrounded values and all
parameters. Each row below used **500 requests, 500 successful, 0 failed**.
Mixed routes are `/`, `/health`, `/index.html`, `/style.css`, and `/hello.txt`.

| Workload | Clients | Elapsed s | Successful req/s | Average ms | p50 ms | p95 ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| /health | 1 | 0.086 | 5798.4 | 0.171 | 0.151 | 0.236 |
| /index.html (cached) | 1 | 0.109 | 4594.3 | 0.216 | 0.190 | 0.306 |
| Mixed (five routes) | 1 | 0.102 | 4894.0 | 0.203 | 0.173 | 0.248 |
| /health | 4 | 0.229 | 2184.8 | 1.810 | 1.773 | 2.589 |
| /index.html (cached) | 4 | 0.243 | 2060.5 | 1.914 | 1.847 | 2.634 |
| Mixed (five routes) | 4 | 0.256 | 1951.1 | 2.028 | 1.960 | 3.044 |
| /health | 12 | 0.288 | 1738.7 | 6.684 | 6.490 | 9.869 |
| /index.html (cached) | 12 | 0.291 | 1716.5 | 6.783 | 6.617 | 9.802 |
| Mixed (five routes) | 12 | 0.300 | 1665.7 | 7.042 | 6.769 | 10.753 |

The initial application-cache miss took 6.661 ms end-to-end; the immediate hit
took 5.138 ms. These are single samples from separate load-client processes,
including client initialization effects; they do not establish a speedup.
The OS filesystem cache was not cleared. Logs verified **one index.html miss
and 1,801 hits** across the run; a cache hit is not itself a throughput metric.

All **4,502 requests succeeded**. Observed thread IDs remained unchanged (main
plus four workers); idle descriptors returned to **six** after each workload.
All client-close records were present, logs had complete lines, all workers joined,
and SIGINT exit status was **0**. Descriptor checks concern process resources,
not TCP TIME_WAIT entries. Thread observations are periodic samples.

Higher concurrency reduced throughput and raised latency in this run. Python
client scheduling/GIL, logging, instrumentation, the unoptimized build, WSL,
filesystem placement, and shared machine load can influence these results;
this experiment does not isolate a server bottleneck. These short, single-run
local development measurements are not universal capacity limits, production
benchmarks, or evidence of a percentage performance improvement.

## Known limitations

- Four fixed workers and blocking client I/O; slow clients can occupy all workers.
- Unbounded task queue; no backpressure, overload response, or client timeout.
- Static files load fully into memory; no file-size limit, streaming, or ranges.
- Single-level filenames only; no URI decoding, query handling, or directory listings.
- Cache capacity is file count, not bytes; no TTL/invalidation, and concurrent
  misses may duplicate disk reads. Concurrent file edits are not snapshot-isolated.
- No drain deadline or forced cancellation. Stalled clients, disk, or logging
  can delay shutdown indefinitely; repeated SIGINT does not force exit.
- SIGTERM does not use the SIGINT graceful-shutdown path.
- One request per connection; no persistent HTTP connections or pipelining.
- No TLS, advanced HTTP body/header handling, or full HTTP compliance.
- Loopback-only Linux target; these local measurements do not establish production capacity.

## Repository guide

| Location | Purpose |
| --- | --- |
| `include/`, `src/` | Component interfaces, implementation, and minimal entry point |
| `public/` | Example HTML, CSS, and text files |
| `tests/` | C++ unit tests and Python integration/load tools |
| `benchmarks/stage13.json` | Intentional, original measured benchmark artifact |
| `docs/architecture.md` | Ownership, synchronization, component and design explanations |
| `ROADMAP.md` | All 14 completed stages and validation history |
| `CMakeLists.txt` | C++17 build, Threads linkage, warnings, and CTest registration |

Generated builds, local tools, Python caches, and common secret files are ignored.
No CI or coverage claims are made. Final scope is complete; no later stage is planned.
