#include <fmt/core.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>

#include <array>
#include <boost/asio.hpp>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <queue>
#include <unordered_set>

namespace io = boost::asio;
using tcp = io::ip::tcp;
using ec = boost::system::error_code;
using fmt::print;

class Session;
using Session_ptr = std::shared_ptr<Session>;

class Session : public std::enable_shared_from_this<Session> {
private:
    tcp::socket socket;
    std::string read_buff;
    std::deque<std::string> msg_list;
    std::unordered_set<Session_ptr>& sessions;

public:
    Session(tcp::socket&& socket, std::unordered_set<Session_ptr>& sessions)
        : socket(std::move(socket)), sessions(sessions) {
        do_read();
    }

    void do_read() {
        io::async_read_until(socket, io::dynamic_buffer(read_buff, 512), '\n',
                             [&](ec e, std::size_t n) {
                                 if (!e) {
                                     print("{} bytes: {}\n", n, read_buff);
                                     post_msg(std::move(read_buff));
                                     do_read();
                                 } else {
                                     print("err in do_read: {}\n", e.what());
                                     stop();
                                 }
                             });
    }

    void post_msg(std::string msg) {
        for (const auto& session : sessions) {
            session->write_msg(msg);
        }
    }

    void write_msg(std::string& msg) {
        bool idle = msg_list.empty();
        msg_list.push_back(msg);

        if (idle) {
            do_write();
        }
    }

    void do_write() {
        io::async_write(socket, io::buffer(msg_list.front()), [&](ec e, std::size_t n) {
            if (!e) {
                msg_list.pop_front();

                if (!msg_list.empty()) {
                    do_write();
                }
            } else {
                print("err in do_write: {}\n", e.what());
                stop();
            }
        });
    }

    void stop() {
        socket.close();
        sessions.erase(shared_from_this());
    }
};

void do_accept(tcp::acceptor& acceptor, std::unordered_set<Session_ptr>& sessions) {
    fmt::print("sessions: {}\n", sessions.size());

    acceptor.async_accept([&](ec e, tcp::socket socket) {
        if (!e) {
            sessions.emplace(std::make_shared<Session>(std::move(socket), sessions));
            do_accept(acceptor, sessions);
        } else {
            print("in function do_accept: {}\n", e.what());
        }
    });
}

int main() {
    std::unordered_set<Session_ptr> sessions;
    io::io_context io_context(1);
    tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 55555));

    do_accept(acceptor, sessions);
    io_context.run();

    return 0;
}
