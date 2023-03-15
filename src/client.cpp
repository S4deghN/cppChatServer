#if defined(__clang__)
    #define _LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS
#endif

#include <coroutine>
#include <unistd.h>
#include <iostream>

#include <boost/asio.hpp>

namespace io = boost::asio;
// namespace log = boost::log;
using tcp = io::ip::tcp;
using ec = boost::system::error_code;

// #define log(a) BOOST_LOG_TRIVIAL(a)

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
            // log(error) << err.what();
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
                // log(error) << "do_read_stdio: " << err.what();
                co_return;
            }

            // log(info) << "input: " << read_buff.substr(0, n - 1);
            co_spawn(io_context, write_socket(std::move(read_buff)), io::detached);
        }
    }

    io::awaitable<void> write_stdio(std::string msg) {
        auto [err, n] = co_await io::async_write(output_descriptor, io::buffer(msg),
                                                 io::as_tuple(io::use_awaitable));
        if (err) {
            // log(error) << "write_stdio: " << err.what();
            co_return;
        }
    }

    io::awaitable<void> do_read_socket() {
        for (std::string read_buff;;) {
            auto [err, n] = co_await io::async_read_until(
                socket, io::dynamic_buffer(read_buff, 512), '\n', io::as_tuple(io::use_awaitable));
            if (err) {
                // log(error) << "do_read_socket: " << err.what();
                terminate();
                co_return;
            }

            // log(info) << n << " bytes: " << read_buff;
            co_spawn(io_context, write_stdio(std::move(read_buff)), io::detached);
        }
    }

    io::awaitable<void> write_socket(std::string msg) {
        auto [err, n] =
            co_await io::async_write(socket, io::buffer(msg), io::as_tuple(io::use_awaitable));
        if (err) {
            // log(error) << "write_socket: " << err.what();
            terminate();
            co_return;
        }
    }

    void terminate() {
        socket.close();
        io_context.stop();
        // log(error) << "connection close due to an error!";
    }
};

void init_logger() {
    // log::core::get()->set_filter(log::trivial::severity >= log::trivial::info);
    // log::add_common_attributes();
    // log::formatter formatter =
    //     log::expressions::stream
    //     << "["
    //     << log::expressions::format_date_time<boost::posix_time::ptime>("TimeStamp", "%H:%M:%S:%f")
    //     << "] ["
    //     << log::trivial::severity << "] " << log::expressions::message;
    // log::add_console_log()->set_formatter(formatter);
}

int main() {
    init_logger();

    io::io_context io_context;
    Client client(io_context, tcp::socket(io_context), tcp::endpoint(tcp::v4(), 55555));
    io_context.run();

    return 0;
}
