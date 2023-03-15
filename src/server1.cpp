#if defined(__clang__)
    #define _LIBCPP_ENABLE_CXX20_REMOVED_TYPE_TRAITS
#endif

#include <spdlog/spdlog.h>

#include <boost/asio.hpp>
#include <coroutine>
#include <mutex>
#include <thread>
#include <unordered_set>

namespace io = boost::asio;
using tcp = io::ip::tcp;

class Session;
using Session_ptr = std::shared_ptr<Session>;
std::mutex sessions_mutex;

class Session : public std::enable_shared_from_this<Session> {
private:
    tcp::socket socket;
    const io::any_io_executor& io_context;
    std::unordered_set<Session_ptr>& sessions;

public:
    Session(tcp::socket&& socket, std::unordered_set<Session_ptr>& sessions)
        : socket(std::move(socket)), io_context(socket.get_executor()), sessions(sessions) {
        io::co_spawn(io_context, do_read(), io::detached);
    }

    io::awaitable<void> do_read() {
        for (std::string read_buff;;) {
            auto [err, n] = co_await io::async_read_until(
                socket, io::dynamic_buffer(read_buff, 512), '\n', io::as_tuple(io::use_awaitable));
            if (err) {
                spdlog::error("do_read: {}", err.what());
                close();
                co_return;
            }

            spdlog::info("{} bytes: {}", n, read_buff.substr(0, n - 1));
            post_msg(std::move(read_buff));
        }
    }

    io::awaitable<void> write(std::string msg) {
        auto [err, n] =
            co_await io::async_write(socket, io::buffer(msg), io::as_tuple(io::use_awaitable));
        if (err) {
            spdlog::error("write: {}", err.what());
            close();
            co_return;
        }
    }

    void post_msg(std::string msg) {
        // TODO: write someting to trace if actually more than one thread tries to
        // aquire the lock
        std::lock_guard<std::mutex> guard(sessions_mutex);
        for (const auto& session : sessions) {
            co_spawn(session->io_context, session->write(msg), io::detached);
        }
    }

    void close() {
        socket.close();
        sessions.erase(shared_from_this());
    }
};

io::awaitable<void> do_accept(tcp::acceptor& acceptor, std::unordered_set<Session_ptr>& sessions) {
    for (;;) {
        spdlog::info("Sessions : {}", sessions.size());
        auto session =
            std::make_shared<Session>(co_await acceptor.async_accept(io::use_awaitable), sessions);
        {
            std::lock_guard<std::mutex> guard(sessions_mutex);
            sessions.emplace(std::move(session));
        }
    }
}

void init_logger() {
}

int main() {
    spdlog::set_pattern("[%H:%M:%S:%f] [%t] [%^%l%$]\t%v");

    io::io_context io_context(16);
    std::unordered_set<Session_ptr> sessions;
    tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 55555));

    co_spawn(io_context, do_accept(acceptor, sessions), io::detached);

    // io_context.run();
    auto count = 16;
    std::vector<std::thread> threads;
    spdlog::info("Threads: {}", count);

    for (unsigned int n = 0; n < count; ++n) {
        threads.emplace_back([&] { io_context.run(); });
    }

    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    return 0;
}
