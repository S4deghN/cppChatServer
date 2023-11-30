#include <boost/asio/co_spawn.hpp>
#if defined(__clang__)
    #define _LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS
#endif

#include <spdlog/spdlog.h>
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <coroutine>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>

#include "message.h"
#include "safe_deque.h"

namespace io = boost::asio;
using tcp = io::ip::tcp;

class Session;
using Session_ptr = std::shared_ptr<Session>;
class PendingSession;
using PendingSession_ptr = std::shared_ptr<PendingSession>;

class Server {
public:
    io::io_context io_context;
    io::ssl::context ssl_context;
    io::strand<io::any_io_executor> sessions_strand;
    io::strand<io::any_io_executor> pending_sessions_strand;
    std::unordered_set<Session_ptr> sessions;
    std::unordered_set<PendingSession_ptr> pending_sessions;

private:
    tcp::acceptor acceptor;
    std::vector<std::thread> thr_vec;

public:
    Server(int threads, tcp::endpoint&& endpoint)
        : io_context(threads)
        , sessions_strand(io::make_strand(io_context))
        , pending_sessions_strand(io::make_strand(io_context))
        , ssl_context(io::ssl::context::tlsv13_server)
        , acceptor(io_context, std::move(endpoint))
    {
        init_ssl();

        io::co_spawn(pending_sessions_strand, do_accept(acceptor), io::detached);

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

    void init_ssl() {
        ssl_context.set_options(io::ssl::context::tlsv13);
        ssl_context.use_certificate_chain_file("server.crt");
        ssl_context.use_private_key_file("server.key", io::ssl::context::pem);
    }

    io::awaitable<void> do_accept(tcp::acceptor& acceptor) {
        spdlog::info("Accepting connection on: {}:{}",
                     acceptor.local_endpoint().address().to_string(),
                     acceptor.local_endpoint().port());
        for (;;) {
            spdlog::info("Sessions : {}", sessions.size());
            auto session = std::make_shared<PendingSession>(co_await acceptor.async_accept(io::use_awaitable), *this);
            pending_sessions.emplace(std::move(session));
        }
    }
};

class PendingSession : public std::enable_shared_from_this<PendingSession> {
private:
    Server& server;
    io::ssl::stream<tcp::socket> ssl_socket;
    std::string read_buffer;
    bool logged_in = false;
    std::string username;

public:
    PendingSession(tcp::socket&& socket, Server& server) :
        server(server),
        ssl_socket(std::move(socket), server.ssl_context)
    {
        io::co_spawn(server.io_context, handshake(), io::detached);
    }

    io::awaitable<void> handshake() {
        ssl_socket.next_layer().set_option(tcp::no_delay(true));

        auto [err] = co_await ssl_socket.async_handshake(io::ssl::stream_base::server, io::as_tuple(io::use_awaitable));
        if (err) {
            spdlog::error("{}: {}", __PRETTY_FUNCTION__, err.what());
            close();
            co_return;
        }

        io::co_spawn(server.io_context, authenticate(), io::detached);
    }

    io::awaitable<void> authenticate() {
        for (;;) {
            auto [err, n] = co_await io::async_read_until(ssl_socket, io::dynamic_buffer(read_buffer), '\n', io::as_tuple(io::use_awaitable));
            if (err) {
                if (err == io::error::operation_aborted) {
                    co_return;
                }
                close();
                spdlog::error("{}: {}", __PRETTY_FUNCTION__, err.what());
                co_return;
            }

            read_buffer.erase(0, n);

            // TODO:
            // if (username is valid) {
            //     send confirmation and create session
            // } else {
            //     send error info and continue the loop
            // }

            io::dispatch(server.sessions_strand,
                [session = std::make_shared<Session>(std::move(ssl_socket), server), self = shared_from_this()] {
                     spdlog::trace("implacing session in to sessions list");
                     self->server.sessions.emplace(std::move(session));
                }
            );

            io::dispatch(server.pending_sessions_strand, [self = shared_from_this()] {
                spdlog::trace("removing pending session from pending sessions list");
                self->server.pending_sessions.erase(self);
            });

            co_return;
        }
    }

    void close() {
        ssl_socket.next_layer().close();
        io::dispatch(server.pending_sessions_strand, [self = shared_from_this()] { self->server.pending_sessions.erase(self); });
        spdlog::info("Pending session closed!");
    }
};

class Session : public std::enable_shared_from_this<Session> {
private:
    Server& server;
    io::ssl::stream<tcp::socket> ssl_socket;
    safe_deque<std::string> write_q;
    std::string read_buffer;
    io::steady_timer timer;

public:
    Session(io::ssl::stream<tcp::socket>&& ssl_socket, Server& server)
        : server(server)
        , ssl_socket(std::move(ssl_socket))
        , timer(server.io_context)
    {
        timer.expires_at(std::chrono::steady_clock::time_point::max());

        io::co_spawn(server.io_context, do_read(), io::detached);
        io::co_spawn(server.io_context, do_write(), io::detached);
    }

    io::awaitable<void> do_read() {
        for (;;) {
            auto [err, n] = co_await io::async_read_until(ssl_socket, io::dynamic_buffer(read_buffer), '\n', io::as_tuple(io::use_awaitable));
            if (err) {
                if (err == io::error::operation_aborted) {
                    co_return;
                }
                close();
                spdlog::error("{}: {}", __PRETTY_FUNCTION__, err.what());
                co_return;
            }

            post_msg(read_buffer);
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

    void post_msg(std::string const& msg) {
        for (auto const& session : server.sessions) {
            session->send(msg);
        }
    }

    void send(std::string const& msg) {
        write_q.push_back(msg);
        timer.cancel_one();
    }

    void close() {
        ssl_socket.next_layer().close();
        timer.cancel();
        io::dispatch(server.sessions_strand, [self = shared_from_this()] { self->server.sessions.erase(self); });
        spdlog::info("Session closed!");
    }
};

int main(int argc, char* argv[]) {
    spdlog::set_pattern("[%H:%M:%S:%f] [%t] [%^%l%$]\t%v");
    spdlog::set_level(spdlog::level::trace);

    int thread_count = argc > 1 ? std::atoi(argv[1]) : 1;
    Server server(thread_count, tcp::endpoint(tcp::v4(), 55555));

    return 0;
}
