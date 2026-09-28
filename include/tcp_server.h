#pragma once

#include <string_view>

class ClientTaskQueue;

class TcpServer {
public:
    explicit TcpServer(int port);
    int run() const;

private:
    static void worker_loop(ClientTaskQueue& tasks);
    static void handle_client(int client_socket);
    static bool communicate_with_client(int client_socket);
    static bool send_response(int client_socket, std::string_view status,
                              std::string_view body);

    int port_;
};
