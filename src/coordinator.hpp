#pragma once
#include "protocol.hpp"
#include <boost/asio.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <iostream>   

using boost::asio::ip::tcp;
using namespace dfs::protocol;

namespace dfs {

struct NodeAddress {
    std::string host;
    unsigned short port;
};

// Round-robin placement: chunk N goes to node (N % numNodes)
class Coordinator {
public:
    explicit Coordinator(std::vector<NodeAddress> nodes)
        : nodes_(std::move(nodes)) {}

    // Splits inputFile into chunks and distributes them round-robin across nodes.
    // Returns the fileId assigned by each node's STORE call, plus a placement
    // map of chunkIndex -> which node holds it, saved as coordinator metadata.
    std::string distributeFile(const std::string& inputFilePath) {
        // For simplicity at this stage: chunk locally first (reusing chunker.hpp
        // logic indirectly isn't wired up yet) — instead, we send the WHOLE file
        // to whichever node is "up next" in round-robin. True per-chunk splitting
        // across nodes comes once nodes can talk to each other (Stage 7+).
        // This stage's goal: prove a coordinator can route different FILES to
        // different nodes, and track which node holds which file.

        size_t nodeIndex = nextNode_;
        nextNode_ = (nextNode_ + 1) % nodes_.size();

        auto& target = nodes_[nodeIndex];
        boost::asio::io_context io;
        tcp::socket socket(io);
        socket.connect(tcp::endpoint(boost::asio::ip::make_address(target.host), target.port));

        std::string filename = std::filesystem::path(inputFilePath).filename().string();
        uint64_t fileSize = std::filesystem::file_size(inputFilePath);

        std::vector<uint8_t> payload;
        writeUint32(payload, static_cast<uint32_t>(filename.size()));
        payload.insert(payload.end(), filename.begin(), filename.end());
        writeUint64(payload, fileSize);

        size_t offset = payload.size();
        payload.resize(offset + fileSize);
        std::ifstream in(inputFilePath, std::ios::binary);
        in.read(reinterpret_cast<char*>(payload.data() + offset), fileSize);

        sendMessage(socket, CMD_STORE, payload);
        Message resp = receiveMessage(socket);
        std::string fileId(resp.payload.begin(), resp.payload.end());

        recordPlacement(fileId, nodeIndex);
        std::cout << "Routed '" << filename << "' -> node " << nodeIndex
                   << " (port " << target.port << "), fileId=" << fileId << "\n";

        return fileId;
    }

    void retrieveFile(const std::string& fileId, const std::string& outputPath) {
        size_t nodeIndex = lookupPlacement(fileId);
        auto& target = nodes_[nodeIndex];

        boost::asio::io_context io;
        tcp::socket socket(io);
        socket.connect(tcp::endpoint(boost::asio::ip::make_address(target.host), target.port));

        std::vector<uint8_t> payload(fileId.begin(), fileId.end());
        sendMessage(socket, CMD_RETRIEVE, payload);

        Message resp = receiveMessage(socket);
        uint64_t fileSize = readUint64(resp.payload.data());

        std::ofstream out(outputPath, std::ios::binary);
        out.write(reinterpret_cast<const char*>(resp.payload.data() + 8), fileSize);

        std::cout << "Retrieved fileId=" << fileId << " from node " << nodeIndex << "\n";
    }

private:
    std::vector<NodeAddress> nodes_;
    size_t nextNode_ = 0;

    void recordPlacement(const std::string& fileId, size_t nodeIndex) {
        nlohmann::json j;
        std::ifstream in("placement.json");
        if (in) in >> j;
        j[fileId] = nodeIndex;
        std::ofstream out("placement.json");
        out << j.dump(2);
    }

    size_t lookupPlacement(const std::string& fileId) {
        std::ifstream in("placement.json");
        nlohmann::json j;
        in >> j;
        return j.at(fileId).get<size_t>();
    }
};

} // namespace dfs