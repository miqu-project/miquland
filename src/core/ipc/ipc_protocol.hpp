#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace miquland::ipc {

constexpr char IPC_MAGIC[4] = {'M', 'I', 'Q', 'U'};

enum class MessageType : uint32_t {
    Command   = 0,  // Request command execution or query
    Subscribe = 1,  // Subscribe client to real-time events
    Response  = 2,  // Server response payload
    Event     = 3,  // Real-time broadcast event
    Error     = 4   // Error response
};

#pragma pack(push, 1)
struct IpcHeader {
    char magic[4];
    uint32_t type;
    uint32_t payload_len;
};
#pragma pack(pop)

inline std::string escape_json(std::string_view str) {
    std::string out;
    out.reserve(str.size() + 8);
    for (char c : str) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

} // namespace miquland::ipc
