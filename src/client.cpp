#if defined(__clang__)
    #define _LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS
#endif

#include <spdlog/spdlog.h>
#include <unistd.h>

#include <boost/asio.hpp>
#include <coroutine>
#include <iostream>

namespace io = boost::asio;
using tcp = io::ip::tcp;
using ec = boost::system::error_code;

class Client {
private:
    io::io_context& io_context;
    tcp::socket socket;
    tcp::endpoint endpoint;
    io::posix::stream_descriptor input_descriptor;
    io::posix::stream_descriptor output_descriptor;

public:
    Client(io::io_context& io_context, tcp::socket&& socket, tcp::endpoint&& endpoint)
        : io_context(io_context),
          socket(std::move(socket)),
          endpoint(std::move(endpoint)),
          input_descriptor(io_context, dup(STDIN_FILENO)),
          output_descriptor(io_context, dup(STDOUT_FILENO)) {
        io::co_spawn(io_context, connect(), io::detached);
    }

    io::awaitable<void> connect() {
        auto [err] = co_await socket.async_connect(endpoint, io::as_tuple(io::use_awaitable));
        if (err) {
            spdlog::error("connect: {}", err.what());
            co_return;
        }

        co_spawn(io_context, do_read_stdio(), io::detached);
        co_spawn(io_context, do_read_socket(), io::detached);
    }

    io::awaitable<void> do_read_stdio() {
        for (std::string read_buff;;) {
            auto [err, n] = co_await boost::asio::async_read_until(
                input_descriptor, io::dynamic_buffer(read_buff), '\n',
                io::as_tuple(io::use_awaitable));
            if (err) {
                spdlog::error("do_read_stdio: {}", err.what());
                co_return;
            }

            co_spawn(io_context, write_socket(std::move(read_buff)), io::detached);
        }
    }

    io::awaitable<void> write_stdio(std::string msg) {
        auto [err, n] = co_await io::async_write(output_descriptor, io::buffer(msg),
                                                 io::as_tuple(io::use_awaitable));
        if (err) {
            spdlog::error("write_stdio: {}", err.what());
            co_return;
        }
    }

    io::awaitable<void> do_read_socket() {
        for (std::string read_buff;;) {
            auto [err, n] = co_await io::async_read_until(
                socket, io::dynamic_buffer(read_buff, 512), '\n', io::as_tuple(io::use_awaitable));
            if (err) {
                spdlog::error("do_read_socket: {}", err.what());
                terminate();
                co_return;
            }

            spdlog::info("{} bytes: {}", n, read_buff.substr(0, n - 1));
            co_spawn(io_context, write_stdio(std::move(read_buff)), io::detached);
        }
    }

    io::awaitable<void> write_socket(std::string msg) {
        auto [err, n] =
            co_await io::async_write(socket, io::buffer(msg), io::as_tuple(io::use_awaitable));
        if (err) {
            spdlog::error("write_socket: {}", err.what());
            terminate();
            co_return;
        }
    }

    void terminate() {
        socket.close();
        io_context.stop();
        spdlog::error("Connection closed due to an error!");
    }
};

int main() {
    spdlog::set_pattern("[%H:%M:%S:%f] [%^%l%$]\t%v");

    io::io_context io_context;
    Client client(io_context, tcp::socket(io_context), tcp::endpoint(tcp::v4(), 55555));
    io_context.run();

    return 0;
}
