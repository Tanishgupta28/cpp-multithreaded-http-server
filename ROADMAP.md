# Development roadmap

Current stage: **Stage 2 complete. Stopped; awaiting authorization for Stage 3.**
Complete one stage per authorized run, then stop for explicit instruction.

| Stage | Scope | Status |
| --- | --- | --- |
| 1 | Project foundation and basic TCP server | Complete |
| 2 | Accept and communicate with TCP clients | Complete |
| 3 | Parse basic HTTP requests | Upcoming |
| 4 | Generate valid HTTP responses | Upcoming |
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
