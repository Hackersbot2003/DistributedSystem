#pragma once
#include "protocol.hpp"
#include "chunker.hpp"
#include "crypto.hpp"
#include "compress.hpp"
#include <boost/asio.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
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
    std::string hash;         // hash of RAW plaintext (stable identity)
    uint64_t originalSize;    // size BEFORE compression — needed to decompress
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

        auto chunkMeta = chunkFileWithMetadata(inputFilePath, tempChunkDir, fileId);

        crypto::Key fileKey = crypto::generateKey();

        std::vector<ChunkPlacement> placements;

        for (const auto& chunk : chunkMeta) {
            size_t primary = nextNode_;
            size_t replica = (nextNode_ + 1) % nodes_.size();
            nextNode_ = (nextNode_ + 1) % nodes_.size();

            std::ifstream in(tempChunkDir + "/" + chunk.sha256 + ".chunk", std::ios::binary);
            std::vector<unsigned char> plaintext(chunk.chunkSize);
            in.read(reinterpret_cast<char*>(plaintext.data()), chunk.chunkSize);

            // Compress THEN encrypt (encrypted data doesn't compress)
            std::vector<unsigned char> compressed = compress::compress(plaintext);
            std::vector<unsigned char> ciphertext = crypto::encrypt(compressed, fileKey);

            sendChunkToNode(primary, chunk.sha256, ciphertext);
            sendChunkToNode(replica, chunk.sha256, ciphertext);

            placements.push_back({chunk.chunkIndex, chunk.sha256, chunk.chunkSize, primary, replica});

            std::cout << "Chunk " << chunk.chunkIndex << " (" << chunk.sha256.substr(0, 8)
                       << "...) " << plaintext.size() << "B -> " << compressed.size()
                       << "B compressed, encrypted, -> primary node " << primary
                       << ", replica node " << replica << "\n";
        }

        saveManifest(fileId, placements, fileKey);
        std::filesystem::remove_all(tempChunkDir);

        return fileId;
    }

    void retrieveFile(const std::string& fileId, const std::string& outputPath) {
        crypto::Key fileKey;
        auto placements = loadManifest(fileId, fileKey);

        std::sort(placements.begin(), placements.end(),
                  [](const ChunkPlacement& a, const ChunkPlacement& b) { return a.index < b.index; });

        std::ofstream out(outputPath, std::ios::binary);

        for (const auto& p : placements) {
            std::vector<unsigned char> ciphertext;
            try {
                ciphertext = fetchCiphertextFromNode(p.primaryNode, p.hash);
                std::cout << "Chunk " << p.index << " fetched from primary node " << p.primaryNode << "\n";
            } catch (std::exception&) {
                std::cout << "Primary node " << p.primaryNode << " failed for chunk " << p.index
                           << ", falling back to replica node " << p.replicaNode << "\n";
                ciphertext = fetchCiphertextFromNode(p.replicaNode, p.hash);
            }

            std::vector<unsigned char> decrypted = crypto::decrypt(ciphertext, fileKey);
            std::vector<unsigned char> plaintext = compress::decompress(decrypted, p.originalSize);

            std::string actualHash = sha256Hex(plaintext.data(), plaintext.size());
            if (actualHash != p.hash) {
                throw std::runtime_error("Integrity check failed on chunk " + std::to_string(p.index));
            }

            out.write(reinterpret_cast<const char*>(plaintext.data()), plaintext.size());
        }
    }

private:
    std::vector<NodeAddress> nodes_;
    size_t nextNode_ = 0;

    void sendChunkToNode(size_t nodeIndex, const std::string& hash, const std::vector<unsigned char>& ciphertext) {
        auto& target = nodes_[nodeIndex];
        boost::asio::io_context io;
        tcp::socket socket(io);
        socket.connect(tcp::endpoint(boost::asio::ip::make_address(target.host), target.port));

        std::vector<uint8_t> payload;
        writeUint32(payload, static_cast<uint32_t>(hash.size()));
        payload.insert(payload.end(), hash.begin(), hash.end());
        writeUint64(payload, ciphertext.size());
        payload.insert(payload.end(), ciphertext.begin(), ciphertext.end());

        sendMessage(socket, CMD_STORE_CHUNK, payload);
        receiveMessage(socket);
    }

    std::vector<unsigned char> fetchCiphertextFromNode(size_t nodeIndex, const std::string& plaintextHash) {
        auto& target = nodes_[nodeIndex];
        boost::asio::io_context io;
        tcp::socket socket(io);
        socket.connect(tcp::endpoint(boost::asio::ip::make_address(target.host), target.port));

        std::vector<uint8_t> payload(plaintextHash.begin(), plaintextHash.end());
        sendMessage(socket, CMD_RETRIEVE_CHUNK, payload);

        Message resp = receiveMessage(socket);
        uint64_t size = readUint64(resp.payload.data());
        return std::vector<unsigned char>(resp.payload.begin() + 8, resp.payload.begin() + 8 + size);
    }

    void saveManifest(const std::string& fileId, const std::vector<ChunkPlacement>& placements,
                       const crypto::Key& key) {
        std::filesystem::create_directories("manifests");
        nlohmann::json j;
        j["fileId"] = fileId;
        j["encryptionKey"] = crypto::keyToHex(key);
        j["chunks"] = nlohmann::json::array();
        for (const auto& p : placements) {
            j["chunks"].push_back({
                {"index", p.index}, {"hash", p.hash}, {"originalSize", p.originalSize},
                {"primaryNode", p.primaryNode}, {"replicaNode", p.replicaNode}
            });
        }
        std::ofstream out("manifests/" + fileId + ".json");
        out << j.dump(2);
    }

    std::vector<ChunkPlacement> loadManifest(const std::string& fileId, crypto::Key& outKey) {
        std::ifstream in("manifests/" + fileId + ".json");
        if (!in) throw std::runtime_error("No manifest for fileId: " + fileId);
        nlohmann::json j;
        in >> j;

        outKey = crypto::keyFromHex(j["encryptionKey"].get<std::string>());

        std::vector<ChunkPlacement> placements;
        for (const auto& c : j["chunks"]) {
            placements.push_back({
                c["index"].get<uint64_t>(), c["hash"].get<std::string>(), c["originalSize"].get<uint64_t>(),
                c["primaryNode"].get<size_t>(), c["replicaNode"].get<size_t>()
            });
        }
        return placements;
    }
};

} // namespace dfs