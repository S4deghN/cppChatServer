#pragma once
// #ifndef MESSAGE_H
// #define MESSAGE_H

//
// Message::data -> 0000000000000000000000000000000000000... (teminates with '\n')
//                  | Message::header() | Message::body()

#define MAX_MESSAGE_SIZE 256

enum class MessageType : uint8_t {
    login = 65,
    text
};

struct Message {
    typedef char DataType;

    struct Header {
        MessageType type;
        // more useful fields ...
    };

    std::vector<DataType> data;

    Message::Header* header_pointer() {
        if (data.size() < sizeof(Header)) {
            return nullptr;
        }

        return (Header*)data.data();
    }

    auto header() {
        return *header_pointer();
    }

    auto set_header(Header header) {
        *header_pointer() = header;
    }

    auto type() {
        return header_pointer()->type;
    }

    auto set_type(MessageType type) {
        header_pointer()->type = type;
    }

    auto body() {
        return data.data() + sizeof(Header);
    }

    auto body_size() {
        // If the division result had reminder we had to manage that and write
        // (int)((float)sizeof(Header) / sizeof(DataType) + 0.5)
        // but for now since the DataType is static and of size 1, we have no
        // problem
        return data.size() - sizeof(Header) / sizeof(DataType);
    }

    auto set_body(DataType const* d, size_t s) {
        if (s > MAX_MESSAGE_SIZE) {
            return 1;
        }

        data.resize(sizeof(Header) + s);
        memcpy(body(), d, s);
        return 0;
    }

    auto append_body(DataType const* d, size_t s) {
        if (data.size() + s > MAX_MESSAGE_SIZE) {
            return 1;
        }

        data.resize(sizeof(Header) + data.size() + s);
        memcpy(body() + data.size(), d, s);
        return 0;
    }
};

// struct Message {
//     enum class Type : uint8_t {
//         login = 65,
//         text
//     };

//     Type* type = (Type*)data;
//     uint8_t* body = data + sizeof(Type);

//     uint8_t data[256] = {0};

//     void set_type(Type t) {
//         *type = t;
//     }

//     void set_body(uint8_t* d, size_t s) {
//         if (s > 256 - sizeof(Type*)) {
//             return;
//         }
//         memcpy(body, d, s);
//     }

// };

// struct Message {
//     enum class type : uint8_t {
//         login = 65,
//         text
//     };
//     struct Header {
//         type type;
//         uint32_t size;
//     };
//     std::vector<uint8_t> data = std::vector<uint8_t>(sizeof(Header));
//     Header* header = (Header*)data.data();
// };
// struct Message {
//     struct meta {
//         message_type type;
//     };
//     meta meta;
//     std::string body;

//     std::array<io::const_buffer, 2> view() {
//         return std::array<io::const_buffer, 2>{io::buffer(&meta, sizeof(Message::meta)), io::buffer(body)};
//     }
// };

// struct Message {
//     struct Header {
//         message_type msg_type;
//         uint32_t msg_size = 0;
//     };
//     Header header;
//     std::vector<uint8_t> body;

//     size_t size() const {
//         return sizeof(Message::Header) + body.size();
//     }

//     friend std::ostream& operator<<(std::ostream& os, const Message& msg) {
//         os << "ID: " << int(msg.header.msg_type) << " Size: " << msg.header.msg_size;
//         return os;
//     }

//     template <typename DataType>
//     friend Message& operator<<(Message& msg, const DataType& data) {
//         static_assert(std::is_standard_layout<DataType>::value, "The data type is too complex to be pushed into a vector");

//         size_t n = msg.body.size();
//         msg.body.resize(msg.body.size() + sizeof(DataType));
//         std::memcpy(msg.body.data() + n, &data, sizeof(DataType));
//         msg.header.msg_size = msg.size();

//         return msg;
//     }

//     template <typename DataType>
//     friend Message& operator>>(Message& msg, const DataType& data) {
//         static_assert(std::is_standard_layout<DataType>::value, "The data type is too complex to be pulled out of a vector");

//         size_t n = msg.body.size() - sizeof(DataType);
//         std::memcpy(&data, msg.body.data() + n, sizeof(DataType));
//         msg.header.msg_size = msg.size();

//         return msg;
//     }
//
// };

// #endif
