#include "tcp_server.h"
#include "client_task_queue.h"
#include "http_request.h"
#include "http_response.h"
#include "http_router.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

namespace {
bool close_socket(int socket_fd) {
    if (close(socket_fd) == -1) {
        std::perror("close");
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
            std::perror("send");
            return false;
        }
        if (sent == 0) {
            std::cerr << "send: no progress sending response.\n";
            return false;
        }
        // send() may accept fewer bytes than requested.
        bytes_sent += static_cast<std::size_t>(sent);
    }
    std::cout << "Sent " << bytes_sent << " response bytes.\n";
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
            std::perror("recv");
            return false;
        }
        if (bytes_received == 0) {
            if (received.empty()) {
                std::cout << "Client closed the connection without sending data.\n";
                return true;
            }
            std::cerr << "Incomplete request line: client closed before CRLF.\n";
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
                std::cerr << "Malformed request line.\n";
                send_response(client_socket, HttpResponse("400 Bad Request", "Bad Request\n"));
                return false;
            }
            std::cout << "Method: " << request.method() << '\n'
                      << "Path: " << request.path() << '\n'
                      << "Version: " << request.version() << '\n';
            const HttpRouter router;
            return send_response(client_socket, router.route(request));
        }
        if (received.size() == max_request_line_bytes) {
            std::cerr << "Request line too long: limit is 1024 bytes including CRLF.\n";
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
        std::cerr << "Client handling failed: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "Client handling failed: unknown exception.\n";
    }
    if (close_socket(client_socket)) {
        std::cout << "Client socket closed." << std::endl;
    }
}

void TcpServer::worker_loop(ClientTaskQueue& tasks) {
    int client_socket;
    while (tasks.pop(client_socket)) {
        // pop has released the queue mutex; all client I/O happens outside it.
        handle_client(client_socket);
    }
}

int TcpServer::run() const {
    ClientTaskQueue tasks;
    const int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        std::perror("socket");
        return 1;
    }

    // Allow restarting after previous connections leave TCP state behind.
    const int reuse_address = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR,
                   &reuse_address, sizeof(reuse_address)) == -1) {
        std::perror("setsockopt");
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
        std::perror("bind");
        close_socket(server_socket);
        return 1;
    }
    if (listen(server_socket, 16) == -1) {
        std::perror("listen");
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
        std::cerr << "Thread pool startup failed: " << error.what() << '\n';
        tasks.close();
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        close_socket(server_socket);
        return 1;
    }

    std::cout << "Listening on 127.0.0.1:" << port_
              << " (4 reusable workers; stop with Ctrl+C)" << std::endl;
    while (true) {
        const int client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket == -1) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("accept");
            break;
        }
        try {
            if (!tasks.push(client_socket)) {
                close_socket(client_socket);
                break;
            }
        } catch (const std::exception& error) {
            // A failed insertion leaves ownership with the accepting thread.
            std::cerr << "Client enqueue failed: " << error.what() << '\n';
            close_socket(client_socket);
            break;
        }
        std::cout << "Client connected." << std::endl;
    }

    close_socket(server_socket);
    tasks.close();
    for (auto& worker : workers) {
        worker.join();
    }
    return 1; // Normal operation runs until the process is stopped externally.
}
