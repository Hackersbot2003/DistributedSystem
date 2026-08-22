#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <openssl/evp.h>
#include <stdexcept>
#include <iomanip>
#include <sstream>
#include "metadata.hpp" 

namespace dfs {

constexpr size_t CHUNK_SIZE = 1024 * 1024; // 1 MB, configurable later

struct ChunkInfo {
    std::string hash;      // hex SHA-256 of this chunk
    size_t index;           // order in the original file
    size_t size;             // bytes in this chunk
};

// Computes SHA-256 of a byte buffer, returns hex string
inline std::string sha256Hex(const unsigned char* data, size_t len) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to create EVP_MD_CTX");

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, data, len) != 1 ||
        EVP_DigestFinal_ex(ctx, hash, &hashLen) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("SHA-256 computation failed");
    }
    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    for (unsigned int i = 0; i < hashLen; ++i)
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    return oss.str();
}

// Splits a file into chunks, writes each to outputDir, returns metadata list
inline std::vector<ChunkInfo> chunkFile(const std::string& inputPath,
                                          const std::string& outputDir) {
    std::filesystem::create_directories(outputDir);

    std::ifstream in(inputPath, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open input file: " + inputPath);

    std::vector<ChunkInfo> chunks;
    std::vector<unsigned char> buffer(CHUNK_SIZE);
    size_t index = 0;

    while (in.read(reinterpret_cast<char*>(buffer.data()), CHUNK_SIZE) || in.gcount() > 0) {
        size_t bytesRead = static_cast<size_t>(in.gcount());
        std::string hash = sha256Hex(buffer.data(), bytesRead);

        std::string chunkPath = outputDir + "/" + hash + ".chunk";
        std::ofstream out(chunkPath, std::ios::binary);
        out.write(reinterpret_cast<char*>(buffer.data()), bytesRead);

        chunks.push_back({hash, index, bytesRead});
        ++index;
    }

    return chunks;
}

// Reassembles chunks (in order) into a single output file
inline void reassembleFile(const std::vector<ChunkInfo>& chunks,
                             const std::string& chunkDir,
                             const std::string& outputPath) {
    std::ofstream out(outputPath, std::ios::binary);
    if (!out) throw std::runtime_error("Cannot open output file: " + outputPath);

    for (const auto& chunk : chunks) {
        std::string chunkPath = chunkDir + "/" + chunk.hash + ".chunk";
        std::ifstream in(chunkPath, std::ios::binary);
        if (!in) throw std::runtime_error("Missing chunk: " + chunkPath);
        out << in.rdbuf();
    }
}

inline std::vector<ChunkMetadata> chunkFileWithMetadata(
        const std::string& inputPath,
        const std::string& chunkDir,
        const std::string& fileId,
        uint64_t version = 1) {

    std::filesystem::create_directories(chunkDir);

    std::ifstream in(inputPath, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open input file: " + inputPath);

    uint64_t originalFileSize = std::filesystem::file_size(inputPath);
    std::string fileName = std::filesystem::path(inputPath).filename().string();

    std::vector<ChunkMetadata> chunks;
    std::vector<unsigned char> buffer(CHUNK_SIZE);
    uint64_t index = 0;

    while (in.read(reinterpret_cast<char*>(buffer.data()), CHUNK_SIZE) || in.gcount() > 0) {
        auto bytesRead = static_cast<uint64_t>(in.gcount());
        std::string hash = sha256Hex(buffer.data(), bytesRead);

        std::ofstream out(chunkDir + "/" + hash + ".chunk", std::ios::binary);
        out.write(reinterpret_cast<char*>(buffer.data()), bytesRead);

        chunks.push_back(ChunkMetadata{
            fileId, fileName, index, bytesRead,
            originalFileSize, hash, version, currentTimestamp()
        });
        ++index;
    }

    return chunks;
}

inline void reassembleFromMetadata(const std::vector<ChunkMetadata>& chunks,
                                     const std::string& chunkDir,
                                     const std::string& outputPath) {
    // Ensure correct order regardless of how they were loaded
    auto sorted = chunks;
    std::sort(sorted.begin(), sorted.end(),
              [](const ChunkMetadata& a, const ChunkMetadata& b) {
                  return a.chunkIndex < b.chunkIndex;
              });

    std::ofstream out(outputPath, std::ios::binary);
    for (const auto& c : sorted) {
        std::ifstream in(chunkDir + "/" + c.sha256 + ".chunk", std::ios::binary);
        if (!in) throw std::runtime_error("Missing chunk: " + c.sha256);
        out << in.rdbuf();
    }
}

} // namespace dfs