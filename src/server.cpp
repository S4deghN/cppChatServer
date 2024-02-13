#include <boost/asio/bind_executor.hpp>
#if defined(__clang__)
    #define _LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS
#endif

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/error.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <coroutine>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>

#include "safe_deque.h"

namespace io = boost::asio;
using tcp = io::ip::tcp;
using ssl_socket = io::ssl::stream<tcp::socket>;

class Session;
using Session_ptr = std::shared_ptr<Session>;

#define on_error(e) on_error_with_info(e, __PRETTY_FUNCTION__)

class Server {
public:
    io::io_context io_context;
    io::ssl::context ssl_context;
    std::unordered_set<Session_ptr> sessions;
    io::strand<io::any_io_executor> sessions_strand;
    tcp::acceptor acceptor;
    std::vector<std::thread> thr_vec;

    Server(int threads, tcp::endpoint&& endpoint)
        : io_context(threads)
        , ssl_context(io::ssl::context::tlsv13_server)
        , sessions_strand(io::make_strand(io_context))
        , acceptor(io_context, endpoint)
    {
        ssl_context.set_options(io::ssl::context::tlsv13);
        ssl_context.use_certificate_chain_file("../data/server.crt");
        ssl_context.use_private_key_file("../data/server.key", io::ssl::context::pem);

        io::co_spawn(io_context, do_accept(acceptor), io::detached);

        spdlog::info("Threads: {}", threads);
        while (threads--) {
            thr_vec.emplace_back([&] { io_context.run(); });
        }
        for (auto& thread : thr_vec) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    io::awaitable<void> do_accept(tcp::acceptor& acceptor);
    void post_msg(std::string const& msg);
};

class Session : public std::enable_shared_from_this<Session> {
public:
    Server& server;
    ssl_socket ssl_socket;
    io::steady_timer timer;
    std::string read_buffer;
    safe_deque<std::string> write_q;
    io::strand<io::any_io_executor> io_strand;

    Session(tcp::socket&& socket, Server& server)
        : server(server)
        , ssl_socket(std::move(socket), server.ssl_context)
        , timer(server.io_context)
        , io_strand(io::make_strand(ssl_socket.get_executor()))
    {
        timer.expires_at(std::chrono::steady_clock::time_point::max());
    }

    io::awaitable<void> handshake() {
        ssl_socket.next_layer().set_option(tcp::no_delay(true));

        auto [err] = co_await ssl_socket.async_handshake(io::ssl::stream_base::server, io::as_tuple(io::use_awaitable));
        if (err) {
            on_error(err);
            co_return;
        }

        io::dispatch(server.sessions_strand, [self = shared_from_this()] { self->server.sessions.emplace(self); });

        io::co_spawn(io_strand, [self = shared_from_this()] { return self->do_read(); }, io::detached);
        io::co_spawn(io_strand, [self = shared_from_this()] { return self->do_write(); }, io::detached);

        // for (auto const& session : server.sessions) {
        //     spdlog::info("Session : {}, refs: {}", (size_t)session.get(), session.use_count());
        // }
    }

    io::awaitable<void> do_read() {
        for (;;) {
            auto [err, n] =
                co_await io::async_read_until(ssl_socket, io::dynamic_buffer(read_buffer, 254), '\n',
                                              io::as_tuple(io::bind_executor(io_strand, io::use_awaitable)));
            if (err) {
                on_error(err);
                co_return;
            }

            // spdlog::info("{} bytes: {}", n, read_buffer.substr(0, n - 1));

            io::dispatch(server.sessions_strand,
                    [self = shared_from_this(), msg = read_buffer.substr(0, n)] { self->server.post_msg(msg); });
            read_buffer.erase(0, n);
        }
    }

    io::awaitable<void> do_write() {
        for (;;) {
            if (!ssl_socket.next_layer().is_open()) {
                co_return;
            }
            if (write_q.size()) {
                auto [err, n] = co_await io::async_write(ssl_socket, io::buffer(write_q.front()),
                                                         io::as_tuple(io::bind_executor(io_strand, io::use_awaitable)));
                if (err) {
                    on_error(err);
                    co_return;
                }
                write_q.pop_front();
            } else {
                auto [err] = co_await timer.async_wait(io::as_tuple(io::bind_executor(io_strand, io::use_awaitable)));
            }
        }
    }

    void on_error_with_info(boost::system::error_code err, const char* func) {
        if (err == io::error::operation_aborted) {
            return;
        }
        spdlog::error("{}: {}", func, err.what());
        ssl_socket.next_layer().close();
        timer.cancel();
        io::dispatch(server.sessions_strand, [self = shared_from_this()] { self->server.sessions.erase(self); });
        spdlog::info("Session closed!");
    }
};

void Server::post_msg(std::string const& msg) {
    for (auto const& session : sessions) {
        session->write_q.push_back(msg);
        session->timer.cancel_one();
    }
}

io::awaitable<void> Server::do_accept(tcp::acceptor& acceptor) {
    spdlog::info("Accepting connection on: {}:{}", acceptor.local_endpoint().address().to_string(), acceptor.local_endpoint().port());
    for (;;) {
        auto session = std::make_shared<Session>(co_await acceptor.async_accept(io::use_awaitable), *this);
        io::co_spawn(io_context, [session] { return session->handshake(); }, io::detached);
    }
}

int main(int argc, char* argv[]) {
    spdlog::set_pattern("[%H:%M:%S:%f] [%t] [%^%l%$]\t%v");
    spdlog::set_level(spdlog::level::trace);

    int thread_count = argc > 1 ? std::atoi(argv[1]) : 1;
    Server server(thread_count, tcp::endpoint(tcp::v4(), 55555));

    return 0;
}
