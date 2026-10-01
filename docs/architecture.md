# Architecture and design decisions

The server has one accepting thread and four reusable workers. Socket calls
remain visible in `TcpServer`; parsing, response construction, routing, static
files, and coordination have separate responsibilities. See the
[README diagram](../README.md#architecture) for the complete request flow.

## Components

| Component | Responsibility and boundary |
| --- | --- |
| `main.cpp` | Validate the optional port, run TcpServer, report fatal startup exceptions. |
| `TcpServer` | Own the listening lifecycle, poll/accept, start/join workers, receive request lines, send responses, close sockets. |
| `HttpRequest` | Validate and own method/path/version; no network I/O. |
| `HttpResponse` | Own status/body/Content-Type and serialize HTTP bytes; no network I/O. |
| `HttpRouter` | Select exact built-in GET routes or delegate to StaticFiles; no mutable route table. |
| `StaticFiles` | Validate names, consult the shared cache, safely open/read regular files, select MIME type. |
| `LRUCache` | Protect cached immutable data and map/list recency invariants; no disk/network I/O. |
| `ClientTaskQueue` | Transfer accepted descriptors through a synchronized FIFO; coordinate wait/wakeup/closure. |
| `Logger` namespace | Serialize complete INFO/ERROR output lines with a dedicated mutex. |
| `ShutdownSignal` | Install SIGINT notification, own the self-pipe, restore the previous handler and close the pipe. |

## Request and resource ownership

1. Main enters `TcpServer::run()`, which owns the listener and local task queue.
   Four joinable workers borrow the queue; it outlives every worker.
2. Main polls the listener and self-pipe, then accepts a client socket. Main owns
   that descriptor until `push` succeeds. Failed insertion leaves main responsible
   for closing it.
3. The queue temporarily owns the accepted task by convention. It stores an
   integer descriptor, not an RAII socket wrapper, and does not close descriptors
   itself. Normal/fatal accepting-flow cleanup closes the queue and drains it.
4. Exactly one worker takes ownership on successful `pop`, then releases the
   queue lock before receiving, parsing, routing, sending, and closing the client.
   Per-client buffers and HTTP objects are local. Exceptions from request handling
   are caught before the worker closes its socket.
5. Static-file misses use local noncopyable RAII descriptor owners for the root
   and file. Binary bytes are read before building immutable cached data.
6. Cache entries use `shared_ptr<const CachedFile>`. A worker copies the handle
   under the cache mutex; eviction cannot invalidate bytes still in use. Responses
   own their body strings, and serialization creates the outgoing string.
7. Main closes the listener, closes the queue, joins all started workers, and
   then lets local objects destruct. `ShutdownSignal` restores the previous
   handler and closes its RAII-managed pipe after workers have joined.

Sockets have explicit cleanup paths; files and the signal pipe use RAII.
Standard strings/containers and lock guards manage their own storage/locks.
This distinction makes the actual ownership model explicit without claiming
that every descriptor is wrapped in an owning class.

## Synchronization

| Lock | Protected state | Scope |
| --- | --- | --- |
| Queue mutex + condition variable | FIFO and closed flag | Push, predicate wait, pop, close |
| Cache mutex | Filename map and recency list | Lookup/promotion, insert/update, eviction |
| Logging mutex | stdout/stderr log writes | One complete line and flush |

The queue predicate is `closed_ || !sockets_.empty()`. Waiting atomically releases
the mutex; waking reacquires it and rechecks the predicate. Notifications are
hints, while the protected predicate carries the state. Push unlocks before
`notify_one`; closure unlocks before `notify_all`. Closed queues reject pushes,
but pop continues until existing tasks are drained. FIFO describes removal
order; scheduling and client speed determine response completion order.

Queue and cache locks are never held during socket I/O, disk reads, or logging.
The logger locks only its output operation. This avoids making unrelated tasks
wait behind a slow client's I/O or a cache miss and avoids nested acquisition of
these three locks. Logging itself can block its callers. Allocation, key work,
or destruction of an evicted entry may still take time under the cache lock;
average O(1) bookkeeping does not promise constant wall-clock latency.

## Shutdown and errors

SIGINT's handler only preserves errno and writes a byte to the nonblocking pipe.
`write` is async-signal-safe; a full pipe already provides a pending notification.
Main gives pipe readiness priority over listener readiness. A concurrent accept
can still transfer one client into the drain. Unaccepted backlog connections have
no response guarantee.

Main closes the listener and queue, wakes sleepers, and joins workers. Active
workers finish their current client and consume remaining tasks before exiting.
Successful SIGINT cleanup returns 0. There is no drain deadline: stalled client,
disk, or logging I/O can prevent completion. SIGTERM retains default behavior.

Malformed requests receive 400 when possible; missing/rejected resources receive
404. Disconnects and worker handling errors affect one client. Cache insertion
failure logs an error but still serves the successfully read file. Fatal startup,
poll, accept, or enqueue failures lead to cleanup and exit 1. Partial worker
startup closes the empty queue and joins workers already created.

## Design decisions and interview tradeoffs

| Decision | Reason and tradeoff |
| --- | --- |
| Fixed pool instead of thread-per-client | Reuses four threads and bounds worker count; four blocked clients can consume all execution capacity. |
| Producer-consumer queue | Separates acceptance from request execution and gives one owner per socket; the unbounded queue can exhaust memory/descriptors under overload. |
| Condition variable instead of busy waiting | Sleeps idle workers until work or closure; predicate checking handles early notifications and spurious wakeups. |
| unordered_map + doubly linked list | Average O(1) lookup and list promotion/eviction without scanning; hashing keys and processing bytes still cost proportional work. |
| Separate cache and queue locks | Accept/dequeue operations need not wait for unrelated cache bookkeeping; each lock protects a small, explicit invariant. |
| Disk reads outside cache lock | A miss does not serialize all file requests behind disk I/O; simultaneous misses may read the same file more than once. |
| Immutable shared cache ownership | Safe use after unlocking/eviction; active requests may retain memory beyond the 16 cached entries. |
| Self-pipe signal notification | Converts a signal into poll readiness using an async-signal-safe operation; normal code performs logging, locking, draining, and joining. |
| Blocking client I/O | Keeps socket flow understandable; absent timeouts, slow readers/writers occupy workers and can delay shutdown indefinitely. |
| Local Stage 13 measurements | Make the workload reproducible; shared client/server CPU, Python/GIL, logging, WSL/filesystem placement, and an unoptimized build limit generalization. |

Static-file safety uses filename rejection, anchored `openat`, `O_NOFOLLOW`, and
regular-file checks, not string concatenation alone. Cache hits reuse previously
validated data and do not recheck disk state. No invalidation or TTL is provided.
The operator controls `public/`; this is not a filesystem sandbox against its owner.

An interview-ready description: "I built a C++17 Linux HTTP server with a single
accepting producer and four worker consumers. A mutex/condition-variable FIFO
hands off socket ownership. Parsing, routing, safe static-file access, and response
construction are separated. A separately locked map/list LRU stores 16 files,
and SIGINT uses a self-pipe so normal code can drain work and join threads. Tests
exercise protocol errors, concurrent clients, cache behavior, and shutdown; the
main limits are blocking I/O, an unbounded queue, and a deliberately small HTTP subset."
