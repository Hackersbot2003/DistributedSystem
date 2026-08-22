#include "storage_node.hpp"
#include "protocol.hpp"
#include <boost/asio.hpp>
#include <iostream>
#include <thread>

using boost::asio::ip::tcp;
using namespace dfs::protocol;

void handleClient(tcp::socket socket, dfs::StorageNode& node) {
    try {
        for (;;) {  // keep handling messages until the client disconnects
            Message req = receiveMessage(socket);

            switch (req.command) {
                case CMD_STORE: {
                    size_t pos = 0;
                    uint32_t nameLen = readUint32(&req.payload[pos]); pos += 4;
                    std::string filename(req.payload.begin() + pos, req.payload.begin() + pos + nameLen);
                    pos += nameLen;
                    uint64_t fileSize = readUint64(&req.payload[pos]); pos += 8;

                    std::string tempPath = "incoming_" + filename;
                    std::ofstream out(tempPath, std::ios::binary);
                    out.write(reinterpret_cast<const char*>(&req.payload[pos]), fileSize);
                    out.close();

                    std::string fileId = node.store(tempPath);
                    std::filesystem::remove(tempPath);

                    std::vector<uint8_t> resp(fileId.begin(), fileId.end());
                    sendMessage(socket, CMD_STORE, resp);
                    std::cout << "Stored file '" << filename << "' as " << fileId << "\n";
                    break;
                }

                case CMD_RETRIEVE: {
                    std::string fileId(req.payload.begin(), req.payload.end());
                    std::string tempPath = "outgoing_" + fileId;

                    node.retrieve(fileId, tempPath);

                    std::ifstream in(tempPath, std::ios::binary);
                    uint64_t fileSize = std::filesystem::file_size(tempPath);
                    std::vector<uint8_t> resp;
                    writeUint64(resp, fileSize);
                    resp.resize(resp.size() + fileSize);
                    in.read(reinterpret_cast<char*>(resp.data() + 8), fileSize);

                    sendMessage(socket, CMD_RETRIEVE, resp);
                    std::filesystem::remove(tempPath);
                    std::cout << "Sent file " << fileId << "\n";
                    break;
                }

                case CMD_LIST: {
                    auto ids = node.listFiles();
                    std::vector<uint8_t> resp;
                    writeUint32(resp, static_cast<uint32_t>(ids.size()));
                    for (const auto& id : ids) {
                        writeUint32(resp, static_cast<uint32_t>(id.size()));
                        resp.insert(resp.end(), id.begin(), id.end());
                    }
                    sendMessage(socket, CMD_LIST, resp);
                    break;
                }

                case CMD_DELETE: {
                    std::string fileId(req.payload.begin(), req.payload.end());
                    node.deleteFile(fileId);
                    sendMessage(socket, CMD_DELETE, {1});
                    std::cout << "Deleted " << fileId << "\n";
                    break;
                }

                default: {
                    std::string msg = "Unknown command";
                    sendMessage(socket, CMD_ERROR, std::vector<uint8_t>(msg.begin(), msg.end()));
                }
            }
        }
    } catch (std::exception& e) {
        std::cout << "Client disconnected.\n";  // receiveMessage throws when the client closes — that's expected, not an error
    }
}
int main(int argc, char* argv[]) {
    try {
        unsigned short port = 6000;
        std::string dataDir = "node_data";

        if (argc >= 2) port = static_cast<unsigned short>(std::stoi(argv[1]));
        if (argc >= 3) dataDir = argv[2];

        dfs::StorageNode node(dataDir);
        boost::asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), port));

        std::cout << "Storage node server listening on port " << port
                   << " (data: " << dataDir << ")...\n";

        for (;;) {
            tcp::socket socket(io_context);
            acceptor.accept(socket);
            std::cout << "Client connected.\n";
            std::thread(handleClient, std::move(socket), std::ref(node)).detach();
        }
    } catch (std::exception& e) {
        std::cerr << "Server exception: " << e.what() << "\n";
    }
}