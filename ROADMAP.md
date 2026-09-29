# Development roadmap

Current stage: **Stage 10 complete. Stopped; awaiting authorization for Stage 11.**
Complete one stage per authorized run, then stop for explicit instruction.

| Stage | Scope | Status |
| --- | --- | --- |
| 1 | Project foundation and basic TCP server | Complete |
| 2 | Accept and communicate with TCP clients | Complete |
| 3 | Parse basic HTTP requests | Complete |
| 4 | Generate valid HTTP responses | Complete |
| 5 | Refactor into clean C++ classes | Complete |
| 6 | Handle multiple clients concurrently | Complete |
| 7 | Implement a fixed-size thread pool | Complete |
| 8 | Implement a thread-safe task queue using std::mutex and std::condition_variable | Complete |
| 9 | HTTP routing, including GET / and GET /health | Complete |
| 10 | Serve static files | Complete |
| 11 | LRU cache using unordered_map and a doubly linked list, average O(1) lookup/update | Upcoming |
| 12 | Graceful shutdown, logging, improved error handling | Upcoming |
| 13 | Testing and basic load/performance testing | Upcoming |
| 14 | Final cleanup, documentation, architecture explanation, GitHub polishing | Upcoming |

Stage 7 introduces workers and fixed-size execution; Stage 8 introduces the reusable synchronized queue. Keep this separation explicit when designing those stages.

## GitHub synchronization

Authenticated as Tanishgupta28. Public repository and `origin`:
https://github.com/Tanishgupta28/cpp-multithreaded-http-server
Push each completed stage after its local commit; verify the remote matches.
If synchronization fails, record it here and preserve the local history.

## Stage 1 validation — 2026-09-27

- Environment: Alpine Linux 3.22 on WSL2, GCC 14.2.0, CMake 3.31.7,
  Python 3.12.14. A dedicated `HTTPServer-Alpine` WSL distribution provides
  the Linux toolchain; no Docker implementation or dependency was added.
- `cmake -S . -B build` and `cmake --build build`: passed, no compiler warnings.
- `ctest --test-dir build --output-on-failure -V`: 1/1 test passed;
  invalid arguments, occupied port, default/custom ports, clean EOF and exit,
  and immediate restart all passed.
- Manual `nc -w 2 127.0.0.1 8080 < /dev/null`: client and server exited 0;
  server printed listening, connected, and socket-closed messages.
- Assumption: bind to loopback for local learning and validation.
- Limits: one accepted connection, no application data exchange, no HTTP,
  no concurrency, and no graceful signal handling yet. Rare socket creation,
  listen, and close failures were reviewed but not fault-injected.

## Stage 2 validation — 2026-09-27

- Same Alpine WSL2 toolchain as Stage 1.
- `cmake -S . -B build` and `cmake --build build`: passed without warnings.
- `ctest --test-dir build --output-on-failure -V`: 1/1 test passed.
  Retained argument, occupied-port, default/custom-port, and restart checks.
  Added reply/EOF verification, received-byte logging, one-byte input,
  embedded NUL, 1,024-byte input, orderly EOF without data, and TCP reset.
- Manual `printf hello | nc -w 2 127.0.0.1 8080`: received
  `Message received.`; server logged `Received 5 bytes: [hello]` and
  `Sent 18 response bytes.`. Client and server exited 0.
- Implementation retries interrupted recv/send calls, handles partial sends,
  suppresses SIGPIPE for sends, reports errors, and closes both sockets.
  Partial sends, send failures, and EINTR were reviewed but not fault-injected.
- Assumption: one receive of at most 1,024 bytes is sufficient for this stage;
  TCP can deliver only part of the client's data in that receive. The reply is
  a fixed acknowledgement, not an echo or an HTTP response.
- Limits: one client, one received chunk, blocking I/O without a timeout,
  no message framing, HTTP parsing, concurrency, routing, or caching.

## Stage 3 validation — 2026-09-27

- Same Alpine WSL2 toolchain as previous stages.
- `cmake -S . -B build` and `cmake --build build`: passed without warnings.
- `ctest --test-dir build --output-on-failure -V`: 1/1 test passed, covering
  valid paths, HTTP/1.0 and HTTP/1.1, fragmented lines, split CRLF, ignored
  trailing headers, an exact 1,024-byte line, malformed fields/separators,
  control and non-ASCII bytes, incomplete EOF, and overlong input. Existing
  port, argument, restart, orderly EOF, response, and reset checks passed.
- Manual `printf 'GET /index.html HTTP/1.1\r\n' | nc -w 2 127.0.0.1 8080`:
  printed method `GET`, path `/index.html`, version `HTTP/1.1`; client received
  `Message received.` and both processes exited 0.
- Assumptions: strict CRLF, single spaces between three fields, syntactically
  valid method tokens, slash-prefixed visible-ASCII paths, HTTP/1.0 or HTTP/1.1.
  Method extraction does not implement method behavior. Queries stay in the path.
- Limits: 1,024 bytes including CRLF; one client and one line; no timeout,
  full URI validation, decoding, header/body parsing, or HTTP response generation.
  Trailing request data is ignored, and unread data at close can cause a reset.
  Invalid lines produce a diagnostic, no reply, and exit status 1. Empty EOF
  retains Stage 2's successful exit. EINTR and partial-send paths remain reviewed
  rather than fault-injected. No concurrency, routing, files, or cache added.

## Stage 4 validation — 2026-09-28

- Same Alpine WSL2 toolchain as previous stages.
- `cmake -S . -B build` and `cmake --build build`: passed without warnings.
- `ctest --test-dir build --output-on-failure -V`: 1/1 test passed. Each valid
  request checks HTTP/1.1 200 OK, Content-Type, Content-Length against body bytes,
  Connection: close, CRLF framing, the blank separator, and the exact body.
  Each malformed, incomplete, or overlong line checks an HTTP 400 response with
  the same framing and length checks. All prior parser and socket checks passed.
- Manual netcat requests: `GET / HTTP/1.1` with Host and a blank line returned
  200 OK and `Hello from C++ HTTP server!\n` (28 body bytes, 112 response bytes).
  `GET / WRONG` returned 400 Bad Request and `Bad Request\n` (12 body bytes,
  105 response bytes). Both clients exited 0; server exits were 0 and 1 respectively.
- Response construction uses the actual body size and the existing partial-send
  loop with EINTR retry and MSG_NOSIGNAL. Both sockets are closed on all normal
  success/error paths. Send failures and partial sends remain reviewed rather
  than fault-injected.
- Assumptions: emit HTTP/1.1 for all replies, independent of the parsed version.
  All syntactically valid request lines get the same fixed response. Malformed,
  incomplete, and overlong input gets 400 if the peer can still receive; empty
  EOF still receives nothing. Rejected requests preserve exit status 1.
- Limits retained: one client, request-line-only parsing, 1,024-byte line limit,
  no timeout, no method-specific behavior or body handling. Unread trailing data
  can cause a reset at close. No routing, static files, concurrency, thread pool,
  caching, advanced headers, or persistent connections added.

## Stage 5 validation — 2026-09-28

- Refactor only: `TcpServer` handles socket setup, bounded receive, logging,
  sending, and explicit cleanup. `HttpRequest` parses and owns the three
  request-line fields. `HttpResponse` owns status/body and serializes the existing
  response format. `main.cpp` only validates the port and starts the server.
- Added three header/source pairs and updated CMake. No inheritance or shared
  ownership; socket descriptors remain local to `TcpServer::run()`, with I/O
  helpers borrowing the client descriptor. Request/response data uses owned strings.
- Same Alpine WSL2 toolchain as previous stages. `cmake -S . -B build` and
  `cmake --build build --clean-first` passed without warnings.
- `ctest --test-dir build --output-on-failure -V`: 1/1 test passed. The existing
  test file is unchanged; it checks response bytes, parsing, fragmented and
  invalid input, port arguments, EOF, reset handling, and exit status.
- Reused the Stage 4 manual netcat checks: valid input returned 200 with a 28-byte
  body (112 response bytes); malformed input returned 400 with a 12-byte body
  (105 response bytes). Client exits were 0; server exits remained 0 and 1.
- No intended externally visible behavior changes. Existing limits remain:
  one client, 1,024-byte request line, no timeout, header/body processing,
  method-specific behavior, routing, static files, concurrency, pool, caching,
  or persistent connections. Unread trailing data can still cause a TCP reset.
  Rare syscall failures and partial-send/EINTR paths were not fault-injected.

## Stage 6 validation — 2026-09-28

- `TcpServer::run()` keeps accepting and creates one detached `std::thread` per
  client. Each worker receives its descriptor by value, handles one request,
  and closes it. Static worker functions do not capture the server object.
  Receive buffers, request/response objects, and send offsets are per-client.
- Thread creation failure closes the unassigned client socket and continues.
  Worker exceptions are caught before client cleanup. An unexpected detach
  failure joins the already-started worker. Backlog is 16; no worker limit.
- CMake links Threads::Threads. `main.cpp`, `HttpRequest`, and `HttpResponse`
  remain unchanged; no thread pool or queue was introduced.
- Same Alpine WSL2 toolchain. `cmake -S . -B build` and
  `cmake --build build --clean-first`: passed without warnings.
- `ctest --test-dir build --output-on-failure -V`: 1/1 test passed with all prior
  request/response, argument, port, EOF, reset, and restart cases retained.
  Per-client exit checks now verify worker completion and a live listener;
  test processes are explicitly stopped after validation.
- Added overlap validation: two incomplete requests stay open while six other
  clients receive mixed 200/400 responses; both slow clients subsequently get
  200 and a new client still succeeds. This demonstrates actual concurrency,
  not just sequential requests to a persistent listener.
- Manual netcat check: fast client returned 200 while another request remained
  incomplete; the slow client later returned 200. A subsequent malformed request
  returned 400 and the server stayed running. Test server was stopped afterward.
- Assumptions/limits: detached threads, unbounded thread count, no idle timeout,
  potentially interleaved console output, and no graceful shutdown. Ctrl+C or a
  fatal accept error ends the process without waiting for workers; the OS
  reclaims remaining sockets. A rare detach failure can block the accept loop
  while joining. Thread creation/detach failures, worker exceptions, and partial
  sends/EINTR were reviewed but not fault-injected. Existing HTTP limitations
  remain; no routing, files, caching, persistent connections, or body handling.

## Stage 7 validation — 2026-09-28

- Four reusable, joinable workers are created once in `TcpServer::run()`. Each
  loops over accept/handle/close on the shared listener. The kernel assigns
  connections to available workers; no per-client thread creation or detach.
- Stage boundary preserved: pending connections use the kernel listen backlog
  (16 requested). No application task queue, condition variable, or general
  thread-pool abstraction. Stage 8 still introduces the reusable synchronized queue.
- Minimum synchronization: one startup mutex/flag prevents acceptance until all
  workers exist. Partial creation failure releases workers with the flag false,
  joins those created, closes the listener, and exits 1. It never locks during
  client handling. A fatal accept error shuts down the listener to wake other
  accept calls; workers are joined before closing it. Active clients may delay
  this error cleanup because there is no timeout or graceful shutdown framework.
- Socket ownership: run owns the listener; the accepting worker owns each client
  through cleanup. Every request retains its own buffer, strings, and send offset.
- `main.cpp`, HTTP classes, and existing CMake Threads::Threads linkage unchanged.
- Same Alpine WSL2 toolchain. `cmake -S . -B build` and
  `cmake --build build --clean-first`: passed without warnings.
- `ctest --test-dir build --output-on-failure -V`: 1/1 passed. All previous cases
  retained. Added Linux /proc thread-ID checks for four workers plus main, four
  slow clients and eight waiting connections, no extra workers at saturation,
  one freed worker serving all eight waiting clients, and unchanged thread IDs
  after all 12 clients complete. No performance claims are inferred.
- Manual netcat validation: four delayed requests plus two additional clients;
  additional responses waited until a worker was free, all six returned 200,
  and thread count stayed five (main plus four workers) during and after service.
  The test server was stopped afterward.
- Limits: four blocked clients saturate the pool; backlog overflow can delay or
  fail connects. No timeouts, application overload response, or signal handling.
  Console output can interleave. Partial startup and fatal-accept cleanup were
  reviewed, not fault-injected; previous syscall-testing limits still apply.
  No routing, static files, caching, persistent connections, or advanced HTTP added.

## Stage 8 validation — 2026-09-28

- Added `ClientTaskQueue` using std::queue<int>, std::mutex, and
  std::condition_variable. One producer in `TcpServer::run()` accepts and pushes;
  four unchanged-count consumers wait/pop, handle a client, close it, and repeat.
- The wait predicate is closed-or-nonempty. Push notifies one worker after
  unlocking. Queue closure notifies all; consumers drain pending work then exit.
  Mutex scope is limited to queue and closed-state access, never client I/O.
- Ownership transfers producer -> queue -> worker. Enqueue failure closes the
  producer-owned socket; workers retain existing per-client cleanup. Listener and
  queue outlive all workers. Partial startup closes the empty queue and joins
  started workers. Accept/enqueue failure stops accepting and drains/joins; slow
  clients can still delay this internal cleanup. No signal framework added.
- Preserved main.cpp, HttpRequest, HttpResponse, all existing HTTP/socket test
  cases, and four worker IDs. Stage 7's kernel backlog remains for unaccepted
  connections; Stage 8 additionally holds accepted descriptors in application FIFO.
- Same Alpine WSL2 toolchain. `cmake -S . -B build` and
  `cmake --build build --clean-first`: passed without warnings.
- `ctest --test-dir build --output-on-failure -V`: 2/2 passed. New C++ tests cover
  FIFO, blocking pop, push wakeup, close wakeup, draining, idempotent close, and
  rejected pushes after close. Integration tests preserve earlier cases and
  verify 12 accepted sockets with four occupied workers, eight queued clients,
  unchanged thread IDs, and one released worker processing waiting tasks.
- Idle validation observed all four workers in Linux futex waits with at most
  two total CPU ticks over 200 ms. Three waves of 32 mixed 200/400 requests passed,
  with sleeping and successful wakeups between waves. Correctness, not a benchmark.
- Manual netcat check: six accepted clients while four workers held partial
  lines, two pending application tasks, all six eventually returned 200. Thread
  count stayed five including main; test process was stopped afterward.
- Limits: unbounded FIFO can exhaust memory/descriptors; four stalled workers
  delay queued clients. No timeout, overload policy, graceful shutdown, serialized
  logging, routing, files, cache, persistent HTTP, or advanced parsing added.
  Startup/allocation failures and rare syscall failures remain reviewed rather
  than fault-injected. Linux /proc idle checks require readable wchan files.

## Stage 9 validation - 2026-09-29

- Added stateless `HttpRouter` with exact, case-sensitive method/path comparisons.
  GET / retains the 28-byte home body; GET /health returns `OK\n` (3 bytes).
  Unmatched pairs, including unsupported methods, return 404 with `Not Found\n`
  (10 bytes). Malformed/incomplete/overlong lines still return 400 before routing.
- `TcpServer` passes a parsed `HttpRequest` to the router, then serializes and
  sends its returned `HttpResponse`. Main, the four-worker pool, producer-consumer
  queue, mutex/predicate wait, ownership, and local buffers remain unchanged.
- Same Alpine WSL2 toolchain. `cmake -S . -B build` and
  `cmake --build build --clean-first` passed without warnings.
- `ctest --test-dir build --output-on-failure -V`: 2/2 passed. Previous parser,
  socket/error, FIFO, worker sleep/wakeup, and reuse scenarios remain covered;
  earlier arbitrary-path 200 expectations now correctly expect 404.
- Added exact route/body/header/Content-Length checks, unsupported-method and
  case/trailing-slash/query checks. Overlapping mixed-route clients complete
  while two slow clients wait. Three waves of 32 mixed 200/400/404 requests
  passed with the same four worker IDs. Existing 12-client application queue
  saturation/drain test also passed. These are correctness checks, not metrics.
- Manual netcat requests on port 19090 verified home 200, health 200, unknown
  path 404, malformed line 400, and POST /health fallback 404. Server stopped
  after validation.
- Assumptions: unmatched method/path pairs uniformly return 404; query strings
  remain part of the exact path. No mutable routing state or route-table lock.
- Limits: no static files (Stage 10), HEAD semantics, POST/body handling, URI
  decoding, persistent connections, timeouts, cache, or graceful shutdown.
  Existing unbounded queue and slow-client limitations remain.

## Stage 10 validation - 2026-09-29

- Added `StaticFiles`, called by HttpRouter after the unchanged GET / and
  GET /health built-ins. Single-level GET filenames map to public/ relative to
  the process working directory. Added index.html, style.css, and hello.txt.
- HttpResponse accepts a Content-Type (text/plain by default). HTML, CSS, text,
  and fallback application/octet-stream responses retain CRLF, byte-counted
  Content-Length, and Connection: close. Socket/queue/worker code is unchanged.
- Filename checks reject traversal, extra separators, dot-prefixed names,
  percent escapes, queries, and fragments. openat anchors file lookup to an
  opened public directory; O_NOFOLLOW rejects root/file symlinks. fstat permits
  regular files only; nonblocking open avoids FIFO hangs. Local RAII closes fds.
- Same Alpine WSL2 toolchain: clean CMake build passed without warnings;
  `ctest --test-dir build --output-on-failure -V`: 3/3 suites passed.
  Prior smoke/queue/concurrency tests remain; the previous missing index.html
  case now uses missing.html because index.html is an implemented resource.
- New tests verify exact HTML/CSS/text bytes, MIME, lengths, a 20,480-byte binary
  body with NULs, empty files, missing files/root, symlink rejection, directories,
  FIFOs, and traversal variants. 42 mixed requests through 12 concurrent clients
  passed. File changes are visible on the next request; no cache was added.
- Manual netcat checks: HTML 200 (300 bytes), CSS 200 (116), text 200 (26),
  and /../README.md 404. The validation server was stopped afterward.
- Assumptions/limits: operator-controlled public directory in working directory;
  no nested paths or decoding. Files are read fully into local memory and should
  remain small; no size limit or snapshot guarantee during concurrent edits.
  No caching, ranges, streaming, new shutdown mechanism, or other future features.
  Existing pool/queue limitations remain. Unsupported methods still return 404.
