#pragma once

#include <string_view>

class TcpServer {
public:
    explicit TcpServer(int port);
    int run() const;

private:
    bool communicate_with_client(int client_socket) const;
    bool send_response(int client_socket, std::string_view status,
                       std::string_view body) const;

    int port_;
};
