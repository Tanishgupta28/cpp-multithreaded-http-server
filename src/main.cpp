#include "tcp_server.h"
#include "logger.h"

#include <charconv>
#include <exception>
#include <iostream>
#include <string_view>

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

    try {
        const TcpServer server(port);
        return server.run();
    } catch (const std::exception& error) {
        Logger::error("Fatal server startup error:");
        Logger::error(error.what());
        return 1;
    }
}
