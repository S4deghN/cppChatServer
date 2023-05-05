#pragma once

//
// Message::data -> 0000000000000000000000000000000000000...'\n'
//                  | Message::header() | Message::body()

#define MAX_MESSAGE_SIZE 256

enum class MessageErrorCode : uint8_t {
    no_error,
    successful_login,
    not_logged_in,
    already_logged_in
};

enum class MessageType : uint8_t {
    login = 65,
    text,
    err
};

class Message : public std::basic_string<char> {
public:
    struct Header {
        MessageType type;
        // more useful fields ...
    };

    Header* const header_pointer() const {
        return (Header* const)this->data();
    }

    Header header() const {
        return *header_pointer();
    }

    void set_header(Header const& header) {
        if (this->size() < sizeof(Header)) {
            this->resize(sizeof(Header));
        }

        *header_pointer() = header;
    }

    MessageType type() const {
        return header_pointer()->type;
    }

    char* body() {
        return this->data() + sizeof(Header);
    }

    size_t body_size() const {
        return this->size() - sizeof(Header);
    }

    auto body_view() {
        return std::string_view(body(), body_size());
    }

    int set_body(std::basic_string_view<char> const& view) {
        this->resize(sizeof(Header) + view.size());
        memcpy(body(), view.data(), view.size());
        return 0;
    }

    int append_body(std::basic_string_view<char> const& view) {
        if (this->size() + view.size() > MAX_MESSAGE_SIZE) {
            return 1;
        }

        this->resize(sizeof(Header) + body_size() + view.size());
        memcpy(body() + this->size(), view.data(), view.size());
        return 0;
    }
};

// struct Message {
//     struct Header {
//         MessageType type;
//         // more useful fields ...
//     };

//     // TODO: use a vector of POD type with some modificatin to member functions
//     std::basic_string<char> data;

//     Message()
//         : data(sizeof(Header), 0) {
//     }
//     Message(size_t size)
//         : data(size, 0) {
//     }
//     Message(Header&& header)
//         : data(sizeof(Header), 0) {
//         set_header(header);
//     }
//     Message(Header&& header, std::basic_string_view<char> body)
//         : data(sizeof(Header) + body.size(), 0) {
//         set_header(header);
//         memcpy(this->body(), body.data(), body.size());
//     }
//     Message(std::basic_string<char> const& string) {
//         data = string;
//     }
//     Message(std::basic_string_view<char> const& string) {
//         data = string;
//     }

//     void clear() {
//         data.resize(0);
//     }

//     Header* const header_pointer() const {
//         return (Header* const)data.data();
//     }

//     auto header() const {
//         return *header_pointer();
//     }

//     void set_header(Header const& header) {
//         *header_pointer() = header;
//     }

//     auto type() const {
//         return header_pointer()->type;
//     }

//     auto set_type(MessageType type) {
//         header_pointer()->type = type;
//     }

//     char* body() {
//         return data.data() + sizeof(Header);
//     }

//     size_t body_size() const {
//         return data.size() - sizeof(Header);
//     }

//     auto body_view() {
//         return std::string_view(body(), body_size());
//     }

//     int set_body(void const* d, size_t s) {
//         if (sizeof(Header) + s > MAX_MESSAGE_SIZE) {
//             return 1;
//         }

//         memset(body(), 0, body_size());
//         data.resize(sizeof(Header) + s);
//         memcpy(body(), d, s);
//         return 0;
//     }

//     int set_body(std::basic_string_view<char> const& view) {
//         if (sizeof(Header) + view.size() > MAX_MESSAGE_SIZE) {
//             return 1;
//         }

//         memset(body(), 0, body_size());
//         data.resize(sizeof(Header) + view.size());
//         memcpy(body(), view.data(), view.size());
//         return 0;
//     }

//     auto append_body(void const* d, size_t s) {
//         if (data.size() + s > MAX_MESSAGE_SIZE) {
//             return 1;
//         }

//         data.resize(sizeof(Header) + data.size() + s);
//         memcpy(body() + data.size(), d, s);
//         return 0;
//     }

//     auto append_body(std::basic_string_view<char> const& view) {
//         if (data.size() + view.size() > MAX_MESSAGE_SIZE) {
//             return 1;
//         }

//         data.resize(sizeof(Header) + data.size() + view.size());
//         memcpy(body() + data.size(), view.data(), view.size());
//         return 0;
//     }
// };
