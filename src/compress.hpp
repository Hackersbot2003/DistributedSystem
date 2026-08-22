#pragma once
#include <zstd.h>
#include <vector>
#include <stdexcept>

namespace dfs::compress {

inline std::vector<unsigned char> compress(const std::vector<unsigned char>& input, int level = 3) {
    size_t bound = ZSTD_compressBound(input.size());
    std::vector<unsigned char> output(bound);

    size_t compressedSize = ZSTD_compress(
        output.data(), bound,
        input.data(), input.size(),
        level
    );

    if (ZSTD_isError(compressedSize))
        throw std::runtime_error(std::string("Zstd compression failed: ") + ZSTD_getErrorName(compressedSize));

    output.resize(compressedSize);
    return output;
}

// originalSize must be known ahead of time (we store it in metadata) —
// Zstd's decompress needs a destination buffer sized correctly up front.
inline std::vector<unsigned char> decompress(const std::vector<unsigned char>& input, size_t originalSize) {
    std::vector<unsigned char> output(originalSize);

    size_t decompressedSize = ZSTD_decompress(
        output.data(), originalSize,
        input.data(), input.size()
    );

    if (ZSTD_isError(decompressedSize))
        throw std::runtime_error(std::string("Zstd decompression failed: ") + ZSTD_getErrorName(decompressedSize));

    if (decompressedSize != originalSize)
        throw std::runtime_error("Decompressed size mismatch");

    output.resize(decompressedSize);
    return output;
}

} // namespace dfs::compress