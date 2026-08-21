#pragma once
#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace dfs {

struct ChunkMetadata {
    std::string fileId;
    std::string fileName;
    uint64_t chunkIndex;
    uint64_t chunkSize;
    uint64_t originalFileSize;
    std::string sha256;
    uint64_t version;
    int64_t createdAt;
};

// Simple UUID v4 generator — good enough for fileId uniqueness here
inline std::string generateFileId() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<int> dist(0, 15);

    const char* hexChars = "0123456789abcdef";
    std::string uuid = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
    for (auto& c : uuid) {
        if (c == 'x') {
            c = hexChars[dist(gen)];
        } else if (c == 'y') {
            c = hexChars[(dist(gen) & 0x3) | 0x8]; // variant bits
        }
    }
    return uuid;
}

inline int64_t currentTimestamp() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// Serializes a file's full chunk list to metadata/<fileId>.json
inline void saveMetadata(const std::string& metadataDir,
                          const std::string& fileId,
                          const std::vector<ChunkMetadata>& chunks) {
    std::filesystem::create_directories(metadataDir);

    nlohmann::json j;
    j["fileId"] = fileId;
    j["chunks"] = nlohmann::json::array();

    for (const auto& c : chunks) {
        j["chunks"].push_back({
            {"fileId", c.fileId},
            {"fileName", c.fileName},
            {"chunkIndex", c.chunkIndex},
            {"chunkSize", c.chunkSize},
            {"originalFileSize", c.originalFileSize},
            {"sha256", c.sha256},
            {"version", c.version},
            {"createdAt", c.createdAt}
        });
    }

    std::ofstream out(metadataDir + "/" + fileId + ".json");
    out << j.dump(2); // pretty-print, 2-space indent
}

// Loads a file's chunk list back from metadata/<fileId>.json
inline std::vector<ChunkMetadata> loadMetadata(const std::string& metadataDir,
                                                 const std::string& fileId) {
    std::ifstream in(metadataDir + "/" + fileId + ".json");
    if (!in) throw std::runtime_error("No metadata found for fileId: " + fileId);

    nlohmann::json j;
    in >> j;

    std::vector<ChunkMetadata> chunks;
    for (const auto& item : j["chunks"]) {
        chunks.push_back({
            item["fileId"].get<std::string>(),
            item["fileName"].get<std::string>(),
            item["chunkIndex"].get<uint64_t>(),
            item["chunkSize"].get<uint64_t>(),
            item["originalFileSize"].get<uint64_t>(),
            item["sha256"].get<std::string>(),
            item["version"].get<uint64_t>(),
            item["createdAt"].get<int64_t>()
        });
    }
    return chunks;
}

} // namespace dfs