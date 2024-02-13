#include <boost/asio/detached.hpp>
#include <chrono>
#include <ctime>
#if defined(__clang__)
    #define _LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS
#endif

#include <spdlog/spdlog.h>
#include <unistd.h>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <chrono>
#include <coroutine>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace io = boost::asio;
using tcp = io::ip::tcp;
using ssl_socket = io::ssl::stream<tcp::socket>;

std::string time_in_string() {// {{{
    using namespace std::chrono;

    // get current time
    auto now = system_clock::now();

    // get number of milliseconds for the current second
    // (remainder after division into seconds)
    auto ms = duration_cast<microseconds>(now.time_since_epoch()) % 1000000;

    // convert to std::time_t in order to convert to std::tm (broken time)
    auto timer = system_clock::to_time_t(now);

    // convert to broken time
    std::tm bt = *std::localtime(&timer);

    std::ostringstream oss;

    oss << std::put_time(&bt, "%H:%M:%S");  // HH:MM:SS
    oss << '.' << std::setfill('0') << std::setw(6) << ms.count() << ": ";

    return oss.str();
}// }}}

class Client {
private:
    io::io_context& io_context;
    io::ssl::context& ssl_context;
    ssl_socket ssl_socket;
    tcp::endpoint endpoint;

    io::posix::stream_descriptor input_descriptor;
    io::posix::stream_descriptor output_descriptor;

    std::string stdio_read_buffer;
    std::string stdio_write_buffer;
    std::string socket_read_buffer;

public:
    Client(io::io_context& io_context, io::ssl::context& ssl_context, tcp::socket&& socket, tcp::endpoint&& endpoint) :
        io_context(io_context),
        ssl_context(ssl_context),
        ssl_socket(std::move(socket), ssl_context),
        endpoint(std::move(endpoint)),
        input_descriptor(io_context, dup(STDIN_FILENO)),
        output_descriptor(io_context, dup(STDOUT_FILENO)) {
    }

    io::awaitable<void> connect() {
        auto [err] = co_await ssl_socket.next_layer().async_connect(endpoint, io::as_tuple(io::use_awaitable));
        if (err) {
            spdlog::error("connect: {}", err.what());
            co_return;
        }

        co_spawn(io_context, handshake(), io::detached);
    }

    io::awaitable<void> handshake() {
        auto [err] = co_await ssl_socket.async_handshake(io::ssl::stream_base::client, io::as_tuple(io::use_awaitable));
        if (err) {
            spdlog::error("handshake: {}", err.what());
        }

        spdlog::info("Connected!");
        co_spawn(io_context, read_stdio(), io::detached);
        co_spawn(io_context, read_socket(), io::detached);
    }

    io::awaitable<void> read_stdio() {
        for (;;)
        {
            try
            {
                auto n = co_await boost::asio::async_read_until(input_descriptor, io::dynamic_buffer(stdio_read_buffer, 254), '\n', io::use_awaitable);
                co_spawn(io_context, write_socket(stdio_read_buffer), io::detached);
                stdio_read_buffer.erase(0, n);
            }
            catch (std::exception& err)
            {
                spdlog::error("stdio_to_socket: {}", err.what());
                close();
            }
        }
    }

    io::awaitable<void> write_socket(std::string msg) {
        co_await io::async_write(ssl_socket, io::buffer(msg), io::use_awaitable);
    }

    io::awaitable<void> read_socket() {
        for (;;)
        {
            try
            {
                auto n = co_await io::async_read_until(ssl_socket, io::dynamic_buffer(socket_read_buffer, 254), '\n', io::use_awaitable);
                co_spawn( io_context, write_stdio(std::move(time_in_string().append(socket_read_buffer))), io::detached);
                socket_read_buffer.erase(0, n);
            }
            catch (std::exception& err)
            {
                spdlog::error("socket_to_stdio: {}", err.what());
                close();
            }
        }
    }

    io::awaitable<void> write_stdio(std::string&& msg) {
        co_await io::async_write(output_descriptor, io::buffer(msg), io::use_awaitable);
    }

    void close() {
        ssl_socket.next_layer().close();
        io_context.stop();
        spdlog::error("Connection closed!");
    }
};

int main() {
    spdlog::set_pattern("[%H:%M:%S:%f] [%^%l%$]\t%v");
    spdlog::set_level(spdlog::level::err);

    io::io_context io_context(1);
    io::ssl::context ssl_context(io::ssl::context::tlsv13_client);
    ssl_context.set_options(io::ssl::context::tlsv13);
    ssl_context.load_verify_file("../data/server.crt");
    ssl_context.set_verify_mode(io::ssl::context::verify_peer);

    Client client(io_context, ssl_context, tcp::socket(io_context), tcp::endpoint(tcp::v4(), 55555));
    co_spawn(io_context, client.connect(), io::detached);
    io_context.run();

    return 0;
}
