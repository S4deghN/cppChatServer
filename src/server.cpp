#include <fmt/core.h>
#include <fmt/ostream.h>
#include <fmt/ranges.h>

#include <array>
#include <boost/asio.hpp>
#include <functional>
#include <iostream>
#include <optional>
#include <queue>
#include <unordered_set>

namespace io = boost::asio;
using tcp = io::ip::tcp;
using ec = boost::system::error_code;
using fmt::print;

class Room {};

class Session {
    using error_handler = std::function<void()>;
    using msg_handler = std::function<void(std::string&&)>;

public:
    explicit Session(tcp::socket&& socket) : socket_(std::move(socket)) {}

    void init(msg_handler&& on_msg, error_handler&& on_error) {
        on_error_ = on_error;
        on_msg_ = on_msg;
        do_read();
    }

    void do_read() {
        io::async_read_until(socket_, io::dynamic_buffer(read_data_, 512), '\n',
                             [&](ec e, std::size_t n) {
                                 if (!e) {
                                     print("{} bytes: {}\n", n, read_data_);
                                     on_msg_(std::move(read_data_));
                                     do_read();
                                 } else {
                                     socket_.close();
                                     print("err in do_read: {}\n", e.what());
                                     on_error_();
                                 }
                             });
    }

    void post_msg(const std::string& msg) {
        bool idle = msg_list_.empty();
        msg_list_.push_back(msg);

        if (idle) {
            do_write();
        }
    }

    void do_write() {
        io::async_write(socket_, io::buffer(msg_list_.front()), [&](ec e, std::size_t n) {
            if (!e) {
                msg_list_.pop_front();

                if (!msg_list_.empty()) {
                    do_write();
                }
            } else {
                socket_.close();
                print("err in do_write: {}\n", e.what());
                on_error_();
            }
        });
    }

private:
    tcp::socket socket_;
    std::string read_data_;
    error_handler on_error_;
    msg_handler on_msg_;
    std::deque<std::string> msg_list_;
};

class Server {
public:
    Server(io::io_context& context, tcp::endpoint&& endpoint)
        : context_(context), acceptor_(context, endpoint) {
        print("server constructed. endpoint: {}\n", endpoint.port());
    }

    void do_accept() {
        acceptor_.async_accept([&](ec e, tcp::socket socket) {
            if (!e) {
                auto session = std::make_shared<Session>(std::move(socket));

                session->init([&](std::string msg) { this->deliver_msg(msg); },
                              [&, weak = std::weak_ptr(session)] {
                                  if (auto shared = weak.lock(); shared) {
                                      sessions_.erase(shared);
                                      print("We are one less\n");
                                  }
                              });

                sessions_.emplace(std::move(session));

                this->do_accept();
            } else {
                print("err in acceptor: {}", e.what());
            }
        });
    }

    void deliver_msg(const std::string& msg) {
        for (const auto& session : sessions_) {
            session->post_msg(msg);
        }
    }

private:
    io::io_context& context_;
    tcp::acceptor acceptor_;
    std::unordered_set<std::shared_ptr<Session>> sessions_;
    std::vector<std::string> msg_;
};

int main() {
    int thread_count = std::thread::hardware_concurrency() * 2;

    io::io_context io_context(2);
    Server srv(io_context, tcp::endpoint(tcp::v4(), 55555));
    srv.do_accept();
    io_context.run();

    return 0;
}
