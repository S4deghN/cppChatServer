#pragma once

#include <cstdint>
#include <vector>
#include <map>

enum class MessageType : uint8_t {
    text = 128,
    login,
};

struct Message {
    union {
        struct {
            MessageType type;
            uint32_t body_size;
        };
        uint8_t head[8];
    };
    std::string body;

    // constexpr bool type_is_valid() const {
    //     switch (type) {
    //         case MessageType::text: 
    //         case MessageType::login:
    //             return true;
    //             break;
    //         default:
    //             return false;
    //             break;
    //     }
    // }
};
