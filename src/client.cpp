#if defined(__clang__)
    #define _LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS
#endif

#include <unistd.h>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <coroutine>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <chrono>
#include <ctime>
#include <deque>

#include <spdlog/spdlog.h>

#include "message.h"

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

    oss << '[' << std::put_time(&bt, "%H:%M:%S");  // HH:MM:SS
    oss << '.' << std::setfill('0') << std::setw(6) << ms.count() << "] ";

    return oss.str();
}// }}}

class Client {
private:
    io::io_context& io_context;
    io::ssl::context& ssl_context;
    ssl_socket ssl_socket;
    tcp::endpoint endpoint;

    std::string stdio_read_buffer;
    std::string socket_read_buffer;
    std::deque<std::string> stdio_write_q;
    std::deque<Message> socket_write_q;
    io::steady_timer stdio_write_timer;
    io::steady_timer socket_write_timer;

    io::posix::stream_descriptor input_descriptor;
    io::posix::stream_descriptor output_descriptor;

public:
    Client(io::io_context& io_context, io::ssl::context& ssl_context, tcp::socket&& socket, tcp::endpoint&& endpoint) :
        io_context(io_context),
        ssl_context(ssl_context),
        ssl_socket(std::move(socket), ssl_context),
        endpoint(std::move(endpoint)),
        stdio_write_timer(io_context),
        socket_write_timer(io_context),
        input_descriptor(io_context, dup(STDIN_FILENO)),
        output_descriptor(io_context, dup(STDOUT_FILENO)) {

        stdio_write_timer.expires_at(std::chrono::steady_clock::time_point::max());
        socket_write_timer.expires_at(std::chrono::steady_clock::time_point::max());

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
        co_spawn(io_context, write_stdio(), io::detached);
        co_spawn(io_context, write_socket(), io::detached);

        stdio_write_q.push_back("use \"/<username>\" to log in\n");
        stdio_write_timer.cancel_one();

    }

    io::awaitable<void> read_stdio() {
        for (;;)
        {
            auto [err, n] = co_await boost::asio::async_read_until(input_descriptor, io::dynamic_buffer(stdio_read_buffer), '\n', io::as_tuple(io::use_awaitable));
            if (err) {
                spdlog::error("read_stdio: {}", err.what());
                close();
            }

            Message msg;
            if (stdio_read_buffer[0] == '/') {
                msg.type = MessageType::login;
                msg.body = stdio_read_buffer.substr(1, n - 1);
                msg.body_size = msg.body.size();
            } else {
                msg.type = MessageType::text;
                msg.body = stdio_read_buffer;
                msg.body_size = msg.body.size();
            }

            socket_write_q.push_back(std::move(msg));
            socket_write_timer.cancel_one();
            stdio_read_buffer.erase(0, n);
        }
    }

    io::awaitable<void> write_socket() {
        for (;;) {
            if (!ssl_socket.next_layer().is_open()) {
                co_return;
            }
            if (!socket_write_q.empty()) {
                auto [err, n] = co_await io::async_write(
                    ssl_socket,
                    std::array{io::buffer(socket_write_q.front().head), io::buffer(socket_write_q.front().body)},
                    io::as_tuple(io::use_awaitable));
                if (err) {
                    spdlog::error("write_socket: {}", err.what());
                    close();
                }
                socket_write_q.pop_front();
            } else {
                auto [err] = co_await socket_write_timer.async_wait(io::as_tuple(io::use_awaitable));
            }
        }
    }

    io::awaitable<void> read_socket() {
        for (;;)
        {
            auto [err, n] = co_await io::async_read_until(ssl_socket, io::dynamic_buffer(socket_read_buffer), '\n', io::as_tuple(io::use_awaitable));
            if (err) {
                spdlog::error("read_socket: {}", err.what());
                close();
            } 
            stdio_write_q.push_back(time_in_string().append(socket_read_buffer.substr(0, n)));
            stdio_write_timer.cancel_one();
            socket_read_buffer.erase(0, n);
        }
    }

    io::awaitable<void> write_stdio() {
        for (;;) {
            if (!stdio_write_q.empty()) {
                auto [err, n] = co_await io::async_write(output_descriptor, io::buffer(stdio_write_q.front()), io::as_tuple(io::use_awaitable));
                if (err) {
                    spdlog::error("write_stdio: {}", err.what());
                    close();
                }
                stdio_write_q.pop_front();
            } else {
                auto [err] = co_await stdio_write_timer.async_wait(io::as_tuple(io::use_awaitable));
            }
        }
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
