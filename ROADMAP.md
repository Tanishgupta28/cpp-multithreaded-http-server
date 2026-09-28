# Development roadmap

Current stage: **Stage 4 complete. Stopped; awaiting authorization for Stage 5.**
Complete one stage per authorized run, then stop for explicit instruction.

| Stage | Scope | Status |
| --- | --- | --- |
| 1 | Project foundation and basic TCP server | Complete |
| 2 | Accept and communicate with TCP clients | Complete |
| 3 | Parse basic HTTP requests | Complete |
| 4 | Generate valid HTTP responses | Complete |
| 5 | Refactor into clean C++ classes | Upcoming |
| 6 | Handle multiple clients concurrently | Upcoming |
| 7 | Implement a fixed-size thread pool | Upcoming |
| 8 | Implement a thread-safe task queue using std::mutex and std::condition_variable | Upcoming |
| 9 | HTTP routing, including GET / and GET /health | Upcoming |
| 10 | Serve static files | Upcoming |
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
