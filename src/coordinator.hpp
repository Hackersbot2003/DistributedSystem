#pragma once
#include "protocol.hpp"
#include "chunker.hpp"
#include <boost/asio.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <nlohmann/json.hpp>

using boost::asio::ip::tcp;
using namespace dfs::protocol;

namespace dfs {

struct NodeAddress {
    std::string host;
    unsigned short port;
};

struct ChunkPlacement {
    uint64_t index;
    std::string hash;
    uint64_t size;
    size_t primaryNode;
    size_t replicaNode;
};

class Coordinator {
public:
    explicit Coordinator(std::vector<NodeAddress> nodes)
        : nodes_(std::move(nodes)) {}

    std::string distributeFile(const std::string& inputFilePath) {
        std::string fileId = generateFileId();
        std::string tempChunkDir = "coord_temp_chunks";

        // Reuse existing chunking logic to split + hash locally first
        auto chunkMeta = chunkFileWithMetadata(inputFilePath, tempChunkDir, fileId);

        std::vector<ChunkPlacement> placements;

        for (const auto& chunk : chunkMeta) {
            size_t primary = nextNode_;
            size_t replica = (nextNode_ + 1) % nodes_.size();
            nextNode_ = (nextNode_ + 1) % nodes_.size();

            std::ifstream in(tempChunkDir + "/" + chunk.sha256 + ".chunk", std::ios::binary);
            std::vector<unsigned char> data(chunk.chunkSize);
            in.read(reinterpret_cast<char*>(data.data()), chunk.chunkSize);

            sendChunkToNode(primary, data);
            sendChunkToNode(replica, data);

            placements.push_back({chunk.chunkIndex, chunk.sha256, chunk.chunkSize, primary, replica});

            std::cout << "Chunk " << chunk.chunkIndex << " (" << chunk.sha256.substr(0, 8)
                       << "...) -> primary node " << primary << ", replica node " << replica << "\n";
        }

        saveManifest(fileId, placements);
        std::filesystem::remove_all(tempChunkDir);

        return fileId;
    }

    void retrieveFile(const std::string& fileId, const std::string& outputPath) {
        auto placements = loadManifest(fileId);

        // Sort by chunk index to reassemble in correct order
        std::sort(placements.begin(), placements.end(),
                  [](const ChunkPlacement& a, const ChunkPlacement& b) { return a.index < b.index; });

        std::ofstream out(outputPath, std::ios::binary);

        for (const auto& p : placements) {
            std::vector<unsigned char> data;
            try {
                data = fetchChunkFromNode(p.primaryNode, p.hash);
                std::cout << "Chunk " << p.index << " fetched from primary node " << p.primaryNode << "\n";
            } catch (std::exception&) {
                std::cout << "Primary node " << p.primaryNode << " failed for chunk " << p.index
                           << ", falling back to replica node " << p.replicaNode << "\n";
                data = fetchChunkFromNode(p.replicaNode, p.hash);
            }
            out.write(reinterpret_cast<const char*>(data.data()), data.size());
        }
    }

private:
    std::vector<NodeAddress> nodes_;
    size_t nextNode_ = 0;

    void sendChunkToNode(size_t nodeIndex, const std::vector<unsigned char>& data) {
        auto& target = nodes_[nodeIndex];
        boost::asio::io_context io;
        tcp::socket socket(io);
        socket.connect(tcp::endpoint(boost::asio::ip::make_address(target.host), target.port));

        std::vector<uint8_t> payload;
        writeUint64(payload, data.size());
        payload.insert(payload.end(), data.begin(), data.end());

        sendMessage(socket, CMD_STORE_CHUNK, payload);
        receiveMessage(socket); // response is the hash; we already know it, just consume it
    }

    std::vector<unsigned char> fetchChunkFromNode(size_t nodeIndex, const std::string& hash) {
        auto& target = nodes_[nodeIndex];
        boost::asio::io_context io;
        tcp::socket socket(io);
        socket.connect(tcp::endpoint(boost::asio::ip::make_address(target.host), target.port));

        std::vector<uint8_t> payload(hash.begin(), hash.end());
        sendMessage(socket, CMD_RETRIEVE_CHUNK, payload);

        Message resp = receiveMessage(socket);
        uint64_t size = readUint64(resp.payload.data());
        return std::vector<unsigned char>(resp.payload.begin() + 8, resp.payload.begin() + 8 + size);
    }

    void saveManifest(const std::string& fileId, const std::vector<ChunkPlacement>& placements) {
        std::filesystem::create_directories("manifests");
        nlohmann::json j;
        j["fileId"] = fileId;
        j["chunks"] = nlohmann::json::array();
        for (const auto& p : placements) {
            j["chunks"].push_back({
                {"index", p.index}, {"hash", p.hash}, {"size", p.size},
                {"primaryNode", p.primaryNode}, {"replicaNode", p.replicaNode}
            });
        }
        std::ofstream out("manifests/" + fileId + ".json");
        out << j.dump(2);
    }

    std::vector<ChunkPlacement> loadManifest(const std::string& fileId) {
        std::ifstream in("manifests/" + fileId + ".json");
        if (!in) throw std::runtime_error("No manifest for fileId: " + fileId);
        nlohmann::json j;
        in >> j;

        std::vector<ChunkPlacement> placements;
        for (const auto& c : j["chunks"]) {
            placements.push_back({
                c["index"].get<uint64_t>(), c["hash"].get<std::string>(), c["size"].get<uint64_t>(),
                c["primaryNode"].get<size_t>(), c["replicaNode"].get<size_t>()
            });
        }
        return placements;
    }
};

} // namespace dfs