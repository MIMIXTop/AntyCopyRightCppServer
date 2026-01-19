#include <boost/asio/io_context.hpp>
#include <print>
#include "Server.hpp"

int main() {
    std::println("Hello lox");
    boost::asio::io_context ioc { 4 };

    Network::Server server(ioc, "127.0.0.1", "8080", "../server.crt", "../server.key");

    server.start();
    ioc.run();

    return 0;
}