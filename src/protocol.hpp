#pragma once
#include <boost/asio.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>

namespace dfs::protocol {

enum Command : uint8_t {
    CMD_STORE = 1,
    CMD_RETRIEVE = 2,
    CMD_LIST = 3,
    CMD_DELETE = 4,
    CMD_STORE_CHUNK = 5,
    CMD_RETRIEVE_CHUNK = 6,
    CMD_ERROR = 255
};

// --- Low-level integer helpers (network byte order) ---

inline void writeUint32(std::vector<uint8_t>& buf, uint32_t val) {
    buf.push_back((val >> 24) & 0xFF);
    buf.push_back((val >> 16) & 0xFF);
    buf.push_back((val >> 8) & 0xFF);
    buf.push_back(val & 0xFF);
}

inline void writeUint64(std::vector<uint8_t>& buf, uint64_t val) {
    for (int i = 7; i >= 0; --i)
        buf.push_back((val >> (i * 8)) & 0xFF);
}

inline uint32_t readUint32(const uint8_t* data) {
    return (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) |
           (uint32_t(data[2]) << 8) | uint32_t(data[3]);
}

inline uint64_t readUint64(const uint8_t* data) {
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) val = (val << 8) | data[i];
    return val;
}

// --- Synchronous send/receive over a connected socket ---
// (We use sync I/O for the actual data transfer within a session —
//  the *server* accepting multiple clients is what's async, via Stage 1's
//  pattern. Each session's own read/write sequence is naturally sequential
//  anyway: read command, then respond — so sync here keeps the code readable.)

inline void sendMessage(boost::asio::ip::tcp::socket& socket,
                         uint8_t command,
                         const std::vector<uint8_t>& payload) {
    std::vector<uint8_t> header;
    header.push_back(command);
    writeUint32(header, static_cast<uint32_t>(payload.size()));

    boost::asio::write(socket, boost::asio::buffer(header));
    if (!payload.empty())
        boost::asio::write(socket, boost::asio::buffer(payload));
}

struct Message {
    uint8_t command;
    std::vector<uint8_t> payload;
};

inline Message receiveMessage(boost::asio::ip::tcp::socket& socket) {
    uint8_t header[5];
    boost::asio::read(socket, boost::asio::buffer(header, 5));

    uint8_t command = header[0];
    uint32_t len = readUint32(header + 1);

    std::vector<uint8_t> payload(len);
    if (len > 0)
        boost::asio::read(socket, boost::asio::buffer(payload));

    return {command, payload};
}

} // namespace dfs::protocol