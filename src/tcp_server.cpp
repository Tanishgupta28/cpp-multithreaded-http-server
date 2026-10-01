#include "tcp_server.h"
#include "client_task_queue.h"
#include "http_request.h"
#include "http_response.h"
#include "http_router.h"
#include "logger.h"
#include "shutdown_signal.h"
#include <poll.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <exception>
#include <string>
#include <thread>

namespace {
bool close_socket(int socket_fd) {
    if (close(socket_fd) == -1) {
        Logger::system_error("close", errno);
        return false;
    }
    return true;
}

} // namespace

TcpServer::TcpServer(int port) : port_(port) {}

bool TcpServer::send_response(int client_socket, const HttpResponse& http_response) {
    const std::string response = http_response.serialize();

    std::size_t bytes_sent = 0;
    while (bytes_sent < response.size()) {
        // Suppress SIGPIPE so a disconnected peer produces an error return.
        const ssize_t sent = send(client_socket, response.data() + bytes_sent,
                                  response.size() - bytes_sent, MSG_NOSIGNAL);
        if (sent == -1) {
            if (errno == EINTR) {
                continue;
            }
            Logger::system_error("send", errno);
            return false;
        }
        if (sent == 0) {
            Logger::error("send: no progress sending response.");
            return false;
        }
        // send() may accept fewer bytes than requested.
        bytes_sent += static_cast<std::size_t>(sent);
    }
    Logger::info("Sent " + std::to_string(bytes_sent) + " response bytes.");
    Logger::info(response.substr(0, response.find("\r\n")));
    return true;
}

bool TcpServer::communicate_with_client(int client_socket) {
    constexpr std::size_t max_request_line_bytes = 1024;
    char buffer[max_request_line_bytes];
    std::string received;
    while (true) {
        ssize_t bytes_received;
        do {
            bytes_received = recv(client_socket, buffer,
                                  max_request_line_bytes - received.size(), 0);
        } while (bytes_received == -1 && errno == EINTR);

        if (bytes_received == -1) {
            Logger::system_error("recv", errno);
            return false;
        }
        if (bytes_received == 0) {
            if (received.empty()) {
                Logger::info("Client closed the connection without sending data.");
                return true;
            }
            Logger::error("Incomplete request line: client closed before CRLF.");
            send_response(client_socket, HttpResponse("400 Bad Request", "Bad Request\n"));
            return false;
        }
        // recv() supplies a byte count, not a null-terminated string or a full line.
        received.append(buffer, static_cast<std::size_t>(bytes_received));
        const auto newline = received.find('\n');
        if (newline != std::string::npos) {
            HttpRequest request;
            if (newline == 0 || received[newline - 1] != '\r'
                || !request.parse_request_line(std::string_view(received.data(), newline - 1))) {
                Logger::error("Malformed request line.");
                send_response(client_socket, HttpResponse("400 Bad Request", "Bad Request\n"));
                return false;
            }
            Logger::info("Method: " + request.method());
            Logger::info("Path: " + request.path());
            Logger::info("Version: " + request.version());
            const HttpRouter router;
            return send_response(client_socket, router.route(request));
        }
        if (received.size() == max_request_line_bytes) {
            Logger::error("Request line too long: limit is 1024 bytes including CRLF.");
            send_response(client_socket, HttpResponse("400 Bad Request", "Bad Request\n"));
            return false;
        }
    }
}

void TcpServer::handle_client(int client_socket) {
    // This worker owns the accepted descriptor, even if request handling throws.
    try {
        communicate_with_client(client_socket);
    } catch (const std::exception& error) {
        Logger::error("Client handling failed:");
        Logger::error(error.what());
    } catch (...) {
        Logger::error("Client handling failed: unknown exception.");
    }
    if (close_socket(client_socket)) {
        Logger::info("Client socket closed.");
    }
}

void TcpServer::worker_loop(ClientTaskQueue& tasks) {
    int client_socket;
    while (tasks.pop(client_socket)) {
        // pop has released the queue mutex; all client I/O happens outside it.
        handle_client(client_socket);
    }
    Logger::info("Worker stopped.");
}

int TcpServer::run() const {
    ShutdownSignal shutdown_signal;
    ClientTaskQueue tasks;
    const int server_socket = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (server_socket == -1) {
        Logger::system_error("socket", errno);
        return 1;
    }

    // Allow restarting after previous connections leave TCP state behind.
    const int reuse_address = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR,
                   &reuse_address, sizeof(reuse_address)) == -1) {
        Logger::system_error("setsockopt", errno);
        close_socket(server_socket);
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    // Socket addresses store the port and IPv4 address in network byte order.
    address.sin_port = htons(static_cast<unsigned short>(port_));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(server_socket, reinterpret_cast<const sockaddr*>(&address),
             sizeof(address)) == -1) {
        Logger::system_error("bind", errno);
        close_socket(server_socket);
        return 1;
    }
    if (listen(server_socket, 16) == -1) {
        Logger::system_error("listen", errno);
        close_socket(server_socket);
        return 1;
    }
    constexpr std::size_t worker_count = 4;
    std::array<std::thread, worker_count> workers;
    try {
        for (auto& worker : workers) {
            worker = std::thread([&tasks] { worker_loop(tasks); });
        }
    } catch (const std::exception& error) {
        Logger::error("Thread pool startup failed:");
        Logger::error(error.what());
        tasks.close();
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        close_socket(server_socket);
        return 1;
    }

    int exit_status = 1;
    // Only the listener is nonblocking; accepted sockets retain blocking I/O.
    // Readiness can become stale if a peer resets before accept().
    try {
        Logger::info("Listening on 127.0.0.1:" + std::to_string(port_)
                     + " (4 reusable workers; stop with Ctrl+C)");
        while (true) {
            pollfd ready[] = {{shutdown_signal.descriptor(), POLLIN, 0},
                              {server_socket, POLLIN, 0}};
            if (poll(ready, 2, -1) == -1) {
                if (errno == EINTR) continue;
                Logger::system_error("poll", errno);
                break;
            }
            if (ready[0].revents & POLLIN) {
                Logger::info("Shutdown requested.");
                exit_status = 0;
                break;
            }
            if (ready[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                Logger::error("Fatal listener readiness error.");
                break;
            }
            if (!(ready[1].revents & POLLIN)) continue;
            const int client_socket = accept(server_socket, nullptr, nullptr);
            if (client_socket == -1) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK
                    || errno == ECONNABORTED) continue;
                Logger::system_error("accept", errno);
                break;
            }
            try {
                if (!tasks.push(client_socket)) {
                    close_socket(client_socket);
                    break;
                }
            } catch (const std::exception& error) {
                // A failed insertion leaves ownership with the accepting thread.
                Logger::error("Client enqueue failed:");
                Logger::error(error.what());
                close_socket(client_socket);
                break;
            }
            Logger::info("Client connected.");
        }
    } catch (const std::exception& error) {
        Logger::error("Fatal accepting flow error:");
        Logger::error(error.what());
    }

    if (!close_socket(server_socket)) exit_status = 1;
    tasks.close();
    Logger::info("Listener closed; queue closed. Draining accepted clients.");
    for (auto& worker : workers) {
        worker.join();
    }
    Logger::info("All workers joined; server stopped.");
    return exit_status;
}
