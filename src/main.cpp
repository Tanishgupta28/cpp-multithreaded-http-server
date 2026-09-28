#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>

bool close_socket(int socket_fd) {
    if (close(socket_fd) == -1) {
        std::perror("close");
        return false;
    }
    return true;
}

bool parse_request_line(std::string_view line) {
    const auto first_space = line.find(' ');
    if (first_space == std::string_view::npos || first_space == 0) {
        return false;
    }
    const auto second_space = line.find(' ', first_space + 1);
    if (second_space == std::string_view::npos || second_space == first_space + 1) {
        return false;
    }
    const auto method = line.substr(0, first_space);
    const auto path = line.substr(first_space + 1, second_space - first_space - 1);
    const auto version = line.substr(second_space + 1);

    // HTTP method tokens allow letters, digits, and these punctuation marks.
    constexpr std::string_view punctuation = "!#$%&'*+-.^_`|~";
    for (unsigned char character : method) {
        const bool letter = (character >= 'A' && character <= 'Z')
                         || (character >= 'a' && character <= 'z');
        const bool digit = character >= '0' && character <= '9';
        if (!letter && !digit && punctuation.find(character) == std::string_view::npos) {
            return false;
        }
    }
    if (path.front() != '/' || (version != "HTTP/1.0" && version != "HTTP/1.1")) {
        return false;
    }
    for (unsigned char character : path) {
        if (character <= 32 || character >= 127) {
            return false;
        }
    }
    std::cout << "Method: " << method << '\n'
              << "Path: " << path << '\n'
              << "Version: " << version << '\n';
    return true;
}

bool send_response(int client_socket, std::string_view status, std::string_view body) {
    std::string response = "HTTP/1.1 ";
    response += status;
    response += "\r\nContent-Type: text/plain\r\nContent-Length: ";
    // Content-Length counts body bytes, including its final newline.
    response += std::to_string(body.size());
    response += "\r\nConnection: close\r\n\r\n";
    response += body;

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

bool communicate_with_client(int client_socket) {
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
            if (newline == 0 || received[newline - 1] != '\r'
                || !parse_request_line(std::string_view(received.data(), newline - 1))) {
                std::cerr << "Malformed request line.\n";
                send_response(client_socket, "400 Bad Request", "Bad Request\n");
                return false;
            }
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

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 2) {
        std::cerr << "Usage: " << argv[0] << " [port: 1-65535]\n";
        return 1;
    }
    if (argc == 2) {
        const std::string_view argument(argv[1]);
        const auto result = std::from_chars(argument.data(),
                                            argument.data() + argument.size(), port);
        if (result.ec != std::errc{} || result.ptr != argument.data() + argument.size()
            || port < 1 || port > 65535) {
            std::cerr << "Invalid port: expected an integer from 1 to 65535.\n";
            return 1;
        }
    }

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
    address.sin_port = htons(static_cast<unsigned short>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(server_socket, reinterpret_cast<const sockaddr*>(&address),
             sizeof(address)) == -1) {
        std::perror("bind");
        close_socket(server_socket);
        return 1;
    }
    if (listen(server_socket, 1) == -1) {
        std::perror("listen");
        close_socket(server_socket);
        return 1;
    }
    std::cout << "Listening on 127.0.0.1:" << port
              << " (one connection, then exit)" << std::endl;

    // accept() creates a client socket; the listening socket stays separate.
    int client_socket;
    do {
        client_socket = accept(server_socket, nullptr, nullptr);
    } while (client_socket == -1 && errno == EINTR);

    if (client_socket == -1) {
        std::perror("accept");
        close_socket(server_socket);
        return 1;
    }
    std::cout << "Client connected." << std::endl;

    const bool communication_succeeded = communicate_with_client(client_socket);
    const bool client_closed = close_socket(client_socket);
    const bool server_closed = close_socket(server_socket);
    if (!communication_succeeded || !client_closed || !server_closed) {
        return 1;
    }
    std::cout << "Sockets closed. Server exiting.\n";
    return 0;
}
