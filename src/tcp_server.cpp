#include "tcp_server.h"
#include "http_request.h"
#include "http_response.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <iostream>
#include <mutex>
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

bool TcpServer::send_response(int client_socket, std::string_view status,
                              std::string_view body) {
    const HttpResponse http_response(status, body);
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
            send_response(client_socket, "400 Bad Request", "Bad Request\n");
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
                send_response(client_socket, "400 Bad Request", "Bad Request\n");
                return false;
            }
            std::cout << "Method: " << request.method() << '\n'
                      << "Path: " << request.path() << '\n'
                      << "Version: " << request.version() << '\n';
            break;
        }
        if (received.size() == max_request_line_bytes) {
            std::cerr << "Request line too long: limit is 1024 bytes including CRLF.\n";
            send_response(client_socket, "400 Bad Request", "Bad Request\n");
            return false;
        }
    }

    return send_response(client_socket, "200 OK", "Hello from C++ HTTP server!\n");
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

void TcpServer::worker_loop(int server_socket) {
    while (true) {
        // The kernel gives each accept() caller its own connected client socket.
        const int client_socket = accept(server_socket, nullptr, nullptr);
        if (client_socket == -1) {
            if (errno == EINTR) {
                continue;
            }
            std::perror("accept");
            // Wake other workers blocked in accept(); run() closes after joining.
            shutdown(server_socket, SHUT_RDWR);
            return;
        }
        std::cout << "Client connected." << std::endl;
        handle_client(client_socket);
    }
}

int TcpServer::run() const {
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
    std::mutex startup_mutex;
    std::unique_lock<std::mutex> startup_lock(startup_mutex);
    bool pool_ready = false;

    try {
        for (auto& worker : workers) {
            worker = std::thread([server_socket, &startup_mutex, &pool_ready] {
                // No client is accepted until every worker has been created.
                // Partial startup can therefore join workers without waiting on clients.
                {
                    std::lock_guard<std::mutex> lock(startup_mutex);
                    if (!pool_ready) {
                        return;
                    }
                }
                worker_loop(server_socket);
            });
        }
    } catch (const std::exception& error) {
        std::cerr << "Thread pool startup failed: " << error.what() << '\n';
        startup_lock.unlock();
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
    pool_ready = true;
    startup_lock.unlock();
    for (auto& worker : workers) {
        worker.join();
    }
    close_socket(server_socket);
    return 1; // Workers normally run indefinitely; returning means accept() failed.
}
