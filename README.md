# Multithreaded HTTP Server

## Overview

An incremental C++ systems programming project working toward a multithreaded
HTTP server. Stage 1 implements a minimal IPv4 TCP server: it accepts one
connection, closes both sockets, and exits. HTTP and concurrency are not yet implemented.

## Features

- IPv4 TCP listener on loopback (`127.0.0.1`).
- Default port 8080, with an optional command-line port from 1 to 65535.
- Socket address reuse, system-call error reporting, and explicit socket cleanup.
- TCP smoke validation, including invalid arguments and occupied ports.

## Architecture

One process follows the socket lifecycle directly in `src/main.cpp`:

```text
socket → setsockopt → bind → listen → accept → close client → close listener
```

`accept()` blocks until a client connects and returns a separate socket for that
client. No application data is read or sent. The process returns 0 on success
and 1 for invalid arguments or system-call errors.

## Technologies / Concepts

C++17, CMake, TCP/IP, POSIX sockets, file descriptors, network byte order,
blocking system calls, and basic error handling. Python 3 is used only for tests.

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
Stage 1 was validated on Alpine Linux 3.22 under WSL2 with GCC 14.2.0,
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

The listener is local-only. It waits for one connection and exits after closing
it; restart the program to accept another connection.

## Usage

In another terminal in the same Linux environment:

```sh
nc 127.0.0.1 8080
```

If netcat is unavailable:

```sh
python3 -c 'import socket; s = socket.create_connection(("127.0.0.1", 8080)); print(repr(s.recv(1))); s.close()'
```

The Python client should print `b''` (EOF). The server prints listening,
client-connected, and socket-closed messages. Do not send an HTTP request:
this stage does not implement the HTTP protocol.

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
accepted connections, clean EOF, successful process exit, immediate restart,
invalid arguments, and occupied-port errors. Validation results are recorded
in [ROADMAP.md](ROADMAP.md). No performance claims are made.

## Roadmap

See [ROADMAP.md](ROADMAP.md) for the 14-stage development plan.

## Learning Outcomes

Explain the difference between listening and accepted sockets, why socket
addresses use network byte order, how a blocking accept behaves, and why every
acquired file descriptor must be closed on success and failure paths.
