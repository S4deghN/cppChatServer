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

    Session(tcp::socket&& socket, Server& server)
        : server(server)
        , ssl_socket(std::move(socket), server.ssl_context)
        , timer(server.io_context)
    {
        timer.expires_at(std::chrono::steady_clock::time_point::max());
    }

    io::awaitable<boost::system::error_code> handshake() {
        ssl_socket.next_layer().set_option(tcp::no_delay(true));
        auto [err] = co_await ssl_socket.async_handshake(io::ssl::stream_base::server, io::as_tuple(io::use_awaitable));
        co_return err;
    }

    io::awaitable<void> do_read() {
        for (;;) {
            auto [err, n] = co_await io::async_read_until(ssl_socket, io::dynamic_buffer(read_buffer, 254), '\n', io::as_tuple(io::use_awaitable));

            // spdlog::info("{} bytes: {}", n, read_buffer.substr(0, n - 1));

            if (err) {
                if (err == io::error::operation_aborted) {
                    co_return;
                }
                close();
                spdlog::error("{}: {}", __PRETTY_FUNCTION__, err.what());
                co_return;
            }

            io::post(server.sessions_strand,
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
                auto [err, n] = co_await io::async_write(ssl_socket, io::buffer(write_q.front()), io::as_tuple(io::use_awaitable));
                write_q.pop_front();
                if (err) {
                    if (err == io::error::operation_aborted) {
                        co_return;
                    }
                    spdlog::error("{}: {}", __PRETTY_FUNCTION__, err.what());
                    close();
                    co_return;
                }
            } else {
                auto [err] = co_await timer.async_wait(io::as_tuple(io::use_awaitable));
            }
        }
    }

    void close() {
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

        io::co_spawn(sessions_strand,
                [session] { return session->handshake(); },
                [&, session] (const std::exception_ptr& /*exp*/, boost::system::error_code err) {
                    if (err) {
                        spdlog::error("{}: {}", __PRETTY_FUNCTION__, err.what());
                        return;
                    }

                    sessions.emplace(session);
                    spdlog::info("Sessions : {}", sessions.size());

                    io::co_spawn(io_context, [session] { return session->do_read(); }, io::detached);
                    io::co_spawn(io_context, [session] { return session->do_write(); }, io::detached);
                    return;
                });

    }
}

int main(int argc, char* argv[]) {
    spdlog::set_pattern("[%H:%M:%S:%f] [%t] [%^%l%$]\t%v");
    spdlog::set_level(spdlog::level::trace);

    int thread_count = argc > 1 ? std::atoi(argv[1]) : 1;
    Server server(thread_count, tcp::endpoint(tcp::v4(), 55555));

    return 0;
}
