# Multithreaded HTTP Server

## Overview

An incremental C++ systems programming project working toward a multithreaded
HTTP server. Stage 2 accepts one IPv4 TCP connection, receives a small chunk of
data, prints it, sends a plain-text acknowledgement, and closes both sockets.
HTTP and concurrency are not yet implemented.

## Features

- IPv4 TCP listener on loopback (`127.0.0.1`).
- Default port 8080, with an optional command-line port from 1 to 65535.
- Socket address reuse, system-call error reporting, and explicit socket cleanup.
- One receive of up to 1,024 bytes, printed using the actual received length.
- Plain-text acknowledgement with partial-send and interrupted-call handling.
- Orderly client disconnect handling and connection-reset error reporting.
- TCP smoke validation, including byte counts, replies, and disconnects.

## Architecture

One process follows the socket lifecycle directly in `src/main.cpp`:

```text
socket → setsockopt → bind → listen → accept → recv → send → close client → close listener
```

`accept()` blocks until a client connects and returns a separate socket for that
client. A small `communicate_with_client()` function receives once and sends
`Message received.\n`. It retries interrupted calls and loops until all response
bytes have been sent. `MSG_NOSIGNAL` lets a failed send report an error instead
of terminating the process with SIGPIPE. Both sockets are closed even if
communication fails. The process returns 0 on success (including EOF before any
data) and 1 for invalid arguments or system-call errors.

TCP is a byte stream: one `recv()` need not contain everything the client sent.
This stage acknowledges only the first received chunk; it does not assemble
messages, wait for a newline, or process further chunks. The receive buffer is
not a C string, so output uses its byte count rather than a null terminator.

## Technologies / Concepts

C++17, CMake, TCP/IP, POSIX sockets, file descriptors, network byte order,
blocking system calls, byte streams, partial sends, and basic error handling.
Python 3 is used only for tests.

## Project Structure

- `src/main.cpp`: argument handling and the visible socket lifecycle.
- `tests/tcp_smoke.py`: real-process TCP checks using Python's standard library.
- `CMakeLists.txt`: executable, compiler warnings, and optional CTest integration.
- `AGENTS.md`: permanent development rules.
- `ROADMAP.md`: stage scope and completion status.

## Build

Use Linux (including a WSL distribution) with a C++17 compiler and CMake
3.16 or newer. Native Windows/MinGW builds are not supported by the POSIX source.
For Ubuntu, prerequisites can be installed with:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake python3
```

On Alpine Linux, run `apk add --no-cache build-base cmake python3` as root.
Stages 1 and 2 were validated on Alpine Linux 3.22 under WSL2 with GCC 14.2.0,
CMake 3.31.7, and Python 3.12.14.

On this Windows development machine, enter the installed Linux environment:

```powershell
wsl -d HTTPServer-Alpine
```

Then navigate to the existing checkout inside Linux:

```sh
cd /mnt/c/Users/skg21/Documents/projects/HTTPS_Server
```

From the project root inside Linux:

```sh
cmake -S . -B build
cmake --build build
```

## Run

```sh
./build/http_server
# Optional custom port:
./build/http_server 9090
```

The listener is local-only. It waits for one connection, then blocks until data
or EOF arrives. After replying (or seeing EOF), it closes the connection and
exits. Restart the program to accept another connection. There is no idle timeout.

## Usage

In another terminal in the same Linux environment:

```sh
nc 127.0.0.1 8080
```

Type `Hello, server!` and press Enter. The client receives `Message received.`
and the server prints the received byte count and data between brackets.

If netcat is unavailable:

```sh
python3 -c 'import socket; s = socket.create_connection(("127.0.0.1", 8080)); s.sendall(b"Hello, server!\n"); print(s.makefile().read(), end=""); s.close()'
```

The Python client prints `Message received.` and reads until EOF. A client that
closes its sending side without sending data gets no reply; the server reports
the disconnect and exits successfully. The response is plain text, not HTTP.

## Testing

With Python 3 available at CMake configuration time:

```sh
ctest --test-dir build --output-on-failure
```

Or run the checks directly:

```sh
python3 tests/tcp_smoke.py ./build/http_server
```

Tests require port 8080 to be free. They verify the default and custom ports,
received data and exact replies, clean EOF, successful process exit, immediate
restart, invalid arguments, occupied ports, one-byte and full-buffer input,
embedded NUL bytes, EOF without data, and connection resets. Partial sends and
interrupted system calls are handled in code but not deterministically forced
by these integration tests. Validation results are recorded
in [ROADMAP.md](ROADMAP.md). No performance claims are made.

## Roadmap

See [ROADMAP.md](ROADMAP.md) for the 14-stage development plan.

## Learning Outcomes

Explain the difference between listening and accepted sockets, why socket
addresses use network byte order, how a blocking accept behaves, and why every
acquired file descriptor must be closed on success and failure paths. Explain
why receive lengths matter, why TCP has no message boundaries, why sending may
require a loop, and how EOF differs from a receive error.
