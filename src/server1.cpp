#include <boost/asio/buffer.hpp>
#include <boost/asio/post.hpp>
#if defined(__clang__)
    #define _LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS
#endif

#include <spdlog/spdlog.h>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <coroutine>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace io = boost::asio;
using tcp = io::ip::tcp;
using ssl_socket = io::ssl::stream<tcp::socket&>;

class Session;
using Session_ptr = std::shared_ptr<Session>;
std::mutex sessions_mutex;

class Server {
private:
    io::io_context io_context;
    tcp::acceptor acceptor;
    std::unordered_set<Session_ptr> sessions;
    std::vector<std::thread> thr_vec;
    io::ssl::context ssl_context;

public:
    Server(int threads, tcp::endpoint&& endpoint)
        : io_context(threads), ssl_context(io::ssl::context::sslv23_server), acceptor(this->io_context, std::move(endpoint)) {
        init_ssl();

        co_spawn(io_context, do_accept(acceptor), io::detached);

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
            auto session = std::make_shared<Session>(co_await acceptor.async_accept(io::use_awaitable), ssl_context, sessions);
            // `sessions` is a shared object among any session that trys to send
            // message to other sessions, also this function itself could be
            // run simultaneously on different threads.
            {
                std::lock_guard<std::mutex> guard(sessions_mutex);
                sessions.emplace(std::move(session));
            }
        }
    }
};

class Session : public std::enable_shared_from_this<Session> {
private:
    tcp::socket socket;
    ssl_socket ssl_socket;
    io::ssl::context& ssl_context;
    io::any_io_executor const& io_context;
    std::unordered_set<Session_ptr>& sessions;

public:
    Session(tcp::socket&& socket, io::ssl::context& ssl_context, std::unordered_set<Session_ptr>& sessions)
        : socket(std::move(socket))
        , ssl_context(ssl_context)
        , ssl_socket(this->socket, ssl_context)
        , io_context(this->socket.get_executor())
        , sessions(sessions) {
        io::co_spawn(io_context, handshake(), io::detached);
    }

    io::awaitable<void> handshake() {
        auto [err] = co_await ssl_socket.async_handshake(io::ssl::stream_base::server, io::as_tuple(io::use_awaitable));
        if (err) {
            spdlog::error("handshake: {}", err.what());
            co_return;
        }

        io::co_spawn(io_context, do_read(), io::detached);
    }

    io::awaitable<void> do_read() {
        for (std::string read_buff;;) {
            auto [err, n] = co_await io::async_read_until(ssl_socket, io::dynamic_buffer(read_buff, 256), '\n',
                                                          io::as_tuple(io::use_awaitable));
            if (err) {
                spdlog::error("do_read: {}", err.what());
                close();
                co_return;
            }

            spdlog::trace("{} bytes: {}", n, std::string_view(read_buff).substr(0, n - 1));
            post_msg(read_buff);
            read_buff.clear();
        }
    }

    io::awaitable<void> write(std::string const& msg) {
        auto [err, n] = co_await io::async_write(ssl_socket, io::buffer(msg), io::as_tuple(io::use_awaitable));
        if (err) {
            spdlog::error("write: {}", err.what());
            close();
            co_return;
        }
    }

    void post_msg(std::string const& msg) {
        std::lock_guard<std::mutex> guard(sessions_mutex);
        for (auto const& session : sessions) {
            co_spawn(session->io_context, session->write(msg), io::detached);
        }
    }

    void close() {
        std::lock_guard<std::mutex> guard(sessions_mutex);
        socket.close();
        sessions.erase(shared_from_this());
        spdlog::error("Session closed!");
    }
};

int main() {
    spdlog::set_pattern("[%H:%M:%S:%f] [%t] [%^%l%$]\t%v");
    spdlog::set_level(spdlog::level::err);

    Server server(8, tcp::endpoint(tcp::v4(), 55555));

    return 0;
}
