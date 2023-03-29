#include <boost/asio/as_tuple.hpp>
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

#include "safe_deque.h"

namespace io = boost::asio;
using tcp = io::ip::tcp;
using ssl_socket = io::ssl::stream<tcp::socket&>;

class Session;
using Session_ptr = std::shared_ptr<Session>;
std::mutex sessions_mutex;

class Server {
private:
    io::io_context io_context;
    io::executor_work_guard<io::io_context::executor_type> work_guard;
    io::strand<io::any_io_executor> shared_sessions_strand;
    tcp::acceptor acceptor;
    std::unordered_set<Session_ptr> sessions;
    std::vector<std::thread> thr_vec;
    io::ssl::context ssl_context;

public:
    Server(int threads = 1, tcp::endpoint&& endpoint = {})
        : io_context(threads)
        , work_guard(io::make_work_guard(io_context))
        , shared_sessions_strand(io::make_strand(io_context))
        , ssl_context(io::ssl::context::sslv23_server)
        , acceptor(io_context, std::move(endpoint)) {
        init_ssl();

        io::co_spawn(shared_sessions_strand, do_accept(acceptor), io::detached);

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
        ssl_context.set_options(io::ssl::context::default_workarounds | io::ssl::context::sslv23 | io::ssl::context::no_sslv2);
        ssl_context.use_certificate_chain_file("cipher.pem");
        ssl_context.use_private_key_file("key.pem", io::ssl::context::pem);
    }

    io::awaitable<void> do_accept(tcp::acceptor& acceptor) {
        spdlog::info("Accepting connection on: {}:{}", acceptor.local_endpoint().address().to_string(),
                     acceptor.local_endpoint().port());
        for (;;) {
            spdlog::info("Sessions : {}", sessions.size());
            auto session = std::make_shared<Session>(co_await acceptor.async_accept(io::use_awaitable), io_context,
                                                     shared_sessions_strand, ssl_context, sessions);
            sessions.emplace(std::move(session));
        }
    }
};

class Session : public std::enable_shared_from_this<Session> {
private:
    tcp::socket socket;
    ssl_socket ssl_socket;
    io::ssl::context& ssl_context;
    io::io_context& io_context;
    io::strand<io::any_io_executor>& shared_sessions_strand;
    std::unordered_set<Session_ptr>& sessions;
    // std::deque<std::string> write_q;
    safe_deque<std::string> write_q;
    io::const_buffer write_buff;
    io::steady_timer timer;
    bool is_writing;

public:
    Session(tcp::socket&& socket, io::io_context& io_context, io::strand<io::any_io_executor>& shared_sessions_strand,
            io::ssl::context& ssl_context, std::unordered_set<Session_ptr>& sessions)
        : socket(std::move(socket))
        , ssl_context(ssl_context)
        , ssl_socket(this->socket, ssl_context)
        , io_context(io_context)
        , shared_sessions_strand(shared_sessions_strand)
        , sessions(sessions)
        , timer(io_context) {
        timer.expires_at(std::chrono::steady_clock::time_point::max());
        io::co_spawn(io_context, handshake(), io::detached);
    }

    io::awaitable<void> handshake() {
        socket.set_option(tcp::no_delay(true));

        auto [err] = co_await ssl_socket.async_handshake(io::ssl::stream_base::server, io::as_tuple(io::use_awaitable));
        if (err) {
            spdlog::error("handshake: {}", err.what());
            close();
            co_return;
        }

        io::co_spawn(io_context, do_read(), io::detached);
        io::co_spawn(io_context, do_write(), io::detached);
    }

    io::awaitable<void> do_read() {
        for (std::string read_buff;;) {
            auto [err, n] =
                co_await io::async_read_until(ssl_socket, io::dynamic_buffer(read_buff), '\n', io::as_tuple(io::use_awaitable));
            if (err) {
                if (err == io::error::operation_aborted) {
                    co_return;
                }
                close();
                spdlog::error("do_read: {}", err.what());
                co_return;
            }

            io::post(shared_sessions_strand, [s = shared_from_this(), msg = read_buff.substr(0, n)]() { s->post_msg(msg); });
            read_buff.erase(0, n);
        }
    }

    io::awaitable<void> do_write() {
        for (;;) {
            if (!socket.is_open()) {
                co_return;
            }

            if (write_q.size()) {
                auto [err, n] = co_await io::async_write(ssl_socket, io::buffer(write_q.front()), io::as_tuple(io::use_awaitable));

                write_q.pop_front();

                if (err) {
                    if (err == io::error::operation_aborted) {
                        co_return;
                    }
                    spdlog::error("write: {}", err.what());
                    close();
                    co_return;
                }
            } else {
                auto [err] = co_await timer.async_wait(io::as_tuple(io::use_awaitable));
            }
        }
    }

    void post_msg(std::string const& msg) {
        for (auto const& session : sessions) {
            session->write_q.push_back(msg);
            session->timer.cancel_one();
        }
    }

    void close() {
        socket.close();
        timer.cancel();
        io::dispatch(shared_sessions_strand, [self = shared_from_this()] { self->sessions.erase(self); });
        spdlog::error("Session closed!");
    }
};

int main(int argc, char* argv[]) {
    spdlog::set_pattern("[%H:%M:%S:%f] [%t] [%^%l%$]\t%v");
    spdlog::set_level(spdlog::level::info);

    Server server(std::atoi(argv[1]), tcp::endpoint(tcp::v4(), 55555));

    return 0;
}
