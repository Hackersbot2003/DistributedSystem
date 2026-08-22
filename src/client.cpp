#include "protocol.hpp"
#include <boost/asio.hpp>
#include <iostream>
#include <fstream>
#include <filesystem>

using boost::asio::ip::tcp;
using namespace dfs::protocol;

std::string storeFile(tcp::socket& socket, const std::string& filePath) {
    std::string filename = std::filesystem::path(filePath).filename().string();
    uint64_t fileSize = std::filesystem::file_size(filePath);

    std::vector<uint8_t> payload;
    writeUint32(payload, static_cast<uint32_t>(filename.size()));
    payload.insert(payload.end(), filename.begin(), filename.end());
    writeUint64(payload, fileSize);

    size_t offset = payload.size();
    payload.resize(offset + fileSize);
    std::ifstream in(filePath, std::ios::binary);
    in.read(reinterpret_cast<char*>(payload.data() + offset), fileSize);

    sendMessage(socket, CMD_STORE, payload);
    Message resp = receiveMessage(socket);
    return std::string(resp.payload.begin(), resp.payload.end());
}

void retrieveFile(tcp::socket& socket, const std::string& fileId, const std::string& outputPath) {
    std::vector<uint8_t> payload(fileId.begin(), fileId.end());
    sendMessage(socket, CMD_RETRIEVE, payload);

    Message resp = receiveMessage(socket);
    uint64_t fileSize = readUint64(resp.payload.data());

    std::ofstream out(outputPath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(resp.payload.data() + 8), fileSize);
}

int main() {
    try {
        boost::asio::io_context io_context;
        tcp::socket socket(io_context);
        socket.connect(tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"), 6000));

        // Create a test file
        std::ofstream f("client_test.txt");
        f << "Hello from the client over the network! ";
        for (int i = 0; i < 50000; ++i) f << "Padding content. ";
        f.close();

        std::string fileId = storeFile(socket, "client_test.txt");
        std::cout << "Server assigned fileId: " << fileId << "\n";

        retrieveFile(socket, fileId, "client_test_downloaded.txt");
        std::cout << "Downloaded to client_test_downloaded.txt\n";

        std::ifstream a("client_test.txt", std::ios::binary), b("client_test_downloaded.txt", std::ios::binary);
        bool same = std::equal(std::istreambuf_iterator<char>(a), std::istreambuf_iterator<char>(),
                                std::istreambuf_iterator<char>(b));
        std::cout << (same ? "MATCH - network round trip successful!\n" : "MISMATCH!\n");

    } catch (std::exception& e) {
        std::cerr << "Client exception: " << e.what() << "\n";
    }
}