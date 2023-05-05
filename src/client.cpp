#include <boost/asio/write.hpp>
#if defined(__clang__)
    #define _LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS
#endif

#include <spdlog/spdlog.h>
#include <unistd.h>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <chrono>
#include <coroutine>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>

#include "message.h"

namespace io = boost::asio;
using tcp = io::ip::tcp;

std::string time_in_string() {
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
    oss << '.' << std::setfill('0') << std::setw(6) << ms.count() << '\t';

    return oss.str();
}

class Client {
private:
    io::io_context& io_context;
    tcp::socket socket;
    tcp::endpoint endpoint;
    io::ssl::context& ssl_context;
    io::ssl::stream<tcp::socket&> ssl_socket;
    io::posix::stream_descriptor input_descriptor;
    io::posix::stream_descriptor output_descriptor;

    std::string username;

public:
    Client(io::io_context& io_context, io::ssl::context& ssl_context, tcp::socket&& socket,
           tcp::endpoint&& endpoint)
        : io_context(io_context)
        , socket(std::move(socket))
        , endpoint(std::move(endpoint))
        , ssl_context(ssl_context)
        , ssl_socket(this->socket, ssl_context)
        , input_descriptor(io_context, dup(STDIN_FILENO))
        , output_descriptor(io_context, dup(STDOUT_FILENO)) {
        io::co_spawn(io_context, connect(), io::detached);
    }

    io::awaitable<void> connect() {
        auto [err] = co_await socket.async_connect(endpoint, io::as_tuple(io::use_awaitable));
        if (err) {
            spdlog::error("connect: {}", err.what());
            co_return;
        }

        co_spawn(io_context, handshake(), io::detached);
    }

    io::awaitable<void> handshake() {
        socket.set_option(tcp::no_delay(true));

        auto [err] = co_await ssl_socket.async_handshake(io::ssl::stream_base::client,
                                                         io::as_tuple(io::use_awaitable));
        if (err) {
            spdlog::error("handshake: {}", err.what());
            co_return;
        }

        spdlog::info("Connected!");
        co_spawn(io_context, do_read_stdio(), io::detached);
        co_spawn(io_context, do_read_socket(), io::detached);

        // io::co_spawn(io_context, login(), io::detached);
    }

    // io::awaitable<void> login() {
    //     // Read username from console blockingly.
    //     io::write(output_descriptor, io::buffer("Please enter your username:\n"));
    //     std::string username;
    //     io::read_until(input_descriptor, io::dynamic_buffer(username, 15), '\n');

    //     // Send username to server asynchronously.
    //     auto [err, n] =
    //         co_await io::async_write(ssl_socket, io::buffer(username), io::as_tuple(io::use_awaitable));
    //     if (err) {
    //         spdlog::error("Sending username: {}", err.what());
    //         close();
    //         co_return;
    //     }

    //     // Get the confirmation message from server
    //     std::string answer;
    //     auto [errr, nn] = co_await io::async_read_until(ssl_socket, io::dynamic_buffer(answer), '\n',
    //                                                     io::as_tuple(io::use_awaitable));
    //     if (errr) {
    //         spdlog::error("Receiving username confirmation: {}", err.what());
    //         close();
    //         co_return;
    //     }

    //     if (answer.substr(0, 2) == "OK") {
    //         spdlog::info("you can chat now! the answer: \"{}\"", answer);
    //         co_spawn(io_context, do_read_stdio(), io::detached);
    //         co_spawn(io_context, do_read_socket(), io::detached);
    //     } else {
    //         spdlog::info("whoops! please try agian. the answer: \"{}\". size: {}", answer,
    //                      answer.size());
    //         for (auto const& c : answer) {
    //             spdlog::info("char: {}", (uint8_t)c);
    //         }
    //         io::co_spawn(io_context, login(), io::detached);
    //     }
    // }

    io::awaitable<void> do_read_stdio() {
        for (std::string line;;) {
            auto [err, n] = co_await boost::asio::async_read_until(
                input_descriptor, io::dynamic_buffer(line, 256), '\n', io::as_tuple(io::use_awaitable));
            if (err) {
                spdlog::error("do_read_stdio: {}", err.what());
                co_return;
            }

            co_spawn(io_context, write_socket(line), io::detached);
            line.clear();
        }
    }

    io::awaitable<void> write_socket(std::string msg) {
        auto [err, n] =
            co_await io::async_write(ssl_socket, io::buffer(msg), io::as_tuple(io::use_awaitable));
        if (err) {
            spdlog::error("write_socket: {}", err.what());
            close();
            co_return;
        }
    }

    io::awaitable<void> do_read_socket() {
        for (std::string msg;;) {
            auto [err, n] = co_await io::async_read_until(ssl_socket, io::dynamic_buffer(msg), '\n',
                                                          io::as_tuple(io::use_awaitable));
            if (err) {
                spdlog::error("do_read_socket: {}", err.what());
                close();
                co_return;
            }

            // spdlog::trace(
            //     "do_read_socket -> type: {}, body_size: {}, data_size: {}, last byte: {}, first "
            //     "byte: {}",
            //     (int)msg.type(), msg.body_size(), msg.data.size(), msg.data.back(), msg.data.front());
            //

            std::string str(time_in_string());
            str.append(msg);
            msg.erase(0, n);

            io::write(output_descriptor, io::buffer(str));
            // co_spawn(io_context, write_stdio(std::move(str)), io::detached);
            // io::write(output_descriptor, io::buffer(str));
        }
    }

    io::awaitable<void> write_stdio(std::string&& msg) {
        auto [err, n] = co_await io::async_write(output_descriptor, io::buffer(msg),
                                                 io::as_tuple(io::use_awaitable));
        if (err) {
            spdlog::error("write_stdio: {}", err.what());
            co_return;
        }
    }

    void close() {
        socket.close();
        io_context.stop();
        spdlog::error("Connection closed due to an error!");
    }
};

int main() {
    spdlog::set_pattern("[%H:%M:%S:%f] [%^%l%$]\t%v");
    spdlog::set_level(spdlog::level::info);

    io::io_context io_context;
    io::ssl::context ssl_context(io::ssl::context::tlsv13_client);

    ssl_context.set_options(io::ssl::context::tlsv13);
    ssl_context.load_verify_file("server.crt");
    ssl_context.set_verify_mode(io::ssl::context::verify_peer);

    Client client(io_context, ssl_context, tcp::socket(io_context), tcp::endpoint(tcp::v4(), 55555));

    io::signal_set signals(io_context, SIGINT, SIGTERM);
    signals.async_wait([&](auto, auto) { io_context.stop(); });

    io_context.run();

    return 0;
}
