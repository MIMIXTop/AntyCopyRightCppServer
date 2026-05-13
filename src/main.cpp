#include <boost/asio/io_context.hpp>
#include <print>
#include "Server.hpp"

#include <cstdlib>
#include <string>

namespace {
std::string envOrDefault(const char* name, std::string fallback) {
    if (const char* value = std::getenv(name); value != nullptr && value[0] != '\0') {
        return value;
    }

    return fallback;
}
}

int main() {
    auto address = envOrDefault("SERVER_ADDRESS", "0.0.0.0");
    auto port = envOrDefault("SERVER_PORT", "8080");

    std::println("Starting server on {}:{}", address, port);
    boost::asio::io_context ioc { 4 };
    Network::Server server(ioc, address, port);

    server.start();
    ioc.run();


    return 0;
}
