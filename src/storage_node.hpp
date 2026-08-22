#pragma once
#include "chunker.hpp"
#include "metadata.hpp"
#include <filesystem>
#include <stdexcept>
#include <iostream>

namespace dfs {

class StorageNode {
public:
    explicit StorageNode(std::string basePath) : basePath_(std::move(basePath)) {
        chunkDir_ = basePath_ + "/chunks";
        metadataDir_ = basePath_ + "/metadata";
        std::filesystem::create_directories(chunkDir_);
        std::filesystem::create_directories(metadataDir_);
    }

    std::string store(const std::string& inputFilePath, uint64_t version = 1) {
        std::string fileId = generateFileId();
        auto chunks = chunkFileWithMetadata(inputFilePath, chunkDir_, fileId, version);
        saveMetadata(metadataDir_, fileId, chunks);
        return fileId;
    }

    void retrieve(const std::string& fileId, const std::string& outputFilePath) {
        auto chunks = loadMetadata(metadataDir_, fileId);
        reassembleFromMetadata(chunks, chunkDir_, outputFilePath);
    }

    bool verify(const std::string& fileId) {
        auto chunks = loadMetadata(metadataDir_, fileId);
        for (const auto& c : chunks) {
            std::string chunkPath = chunkDir_ + "/" + c.sha256 + ".chunk";
            if (!std::filesystem::exists(chunkPath)) {
                std::cerr << "Missing chunk: " << c.sha256 << "\n";
                return false;
            }

            std::ifstream in(chunkPath, std::ios::binary);
            std::vector<unsigned char> buf(std::filesystem::file_size(chunkPath));
            in.read(reinterpret_cast<char*>(buf.data()), buf.size());

            std::string actualHash = sha256Hex(buf.data(), buf.size());
            if (actualHash != c.sha256) {
                std::cerr << "Hash mismatch on chunk " << c.chunkIndex
                          << ": expected " << c.sha256 << ", got " << actualHash << "\n";
                return false;
            }
        }
        return true;
    }

    void deleteFile(const std::string& fileId) {
        auto chunks = loadMetadata(metadataDir_, fileId);
        for (const auto& c : chunks) {
            std::filesystem::remove(chunkDir_ + "/" + c.sha256 + ".chunk");
        }
        std::filesystem::remove(metadataDir_ + "/" + fileId + ".json");
    }

    // Stores a raw chunk (server computes the hash itself)
std::string storeChunk(const std::vector<unsigned char>& data) {
    std::string hash = sha256Hex(data.data(), data.size());
    std::ofstream out(chunkDir_ + "/" + hash + ".chunk", std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()), data.size());
    return hash;
}

// Retrieves a raw chunk by hash
std::vector<unsigned char> retrieveChunk(const std::string& hash) {
    std::string path = chunkDir_ + "/" + hash + ".chunk";
    if (!std::filesystem::exists(path))
        throw std::runtime_error("Chunk not found: " + hash);

    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> data(std::filesystem::file_size(path));
    in.read(reinterpret_cast<char*>(data.data()), data.size());
    return data;
}

    std::vector<std::string> listFiles() {
        std::vector<std::string> ids;
        for (const auto& entry : std::filesystem::directory_iterator(metadataDir_)) {
            if (entry.path().extension() == ".json") {
                ids.push_back(entry.path().stem().string());
            }
        }
        return ids;
    }

private:
    std::string basePath_;
    std::string chunkDir_;
    std::string metadataDir_;
};

} // namespace dfs