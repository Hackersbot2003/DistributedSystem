#include "storage_node.hpp"
#include <iostream>
#include <fstream>

int main() {
    try {
        dfs::StorageNode node("node_data"); // everything lives under ./node_data now

        const std::string inputFile = "test_input.txt";
        {
            std::ofstream f(inputFile);
            f << "Hello, distributed file storage! ";
            for (int i = 0; i < 50000; ++i) f << "Some repeated content to pad the file. ";
        }

        std::string fileId = node.store(inputFile);
        std::cout << "Stored file, fileId = " << fileId << "\n";

        std::cout << "Verify: " << (node.verify(fileId) ? "OK" : "FAILED") << "\n";

        node.retrieve(fileId, "test_output.txt");
        std::cout << "Retrieved to test_output.txt\n";

        std::ifstream a(inputFile, std::ios::binary), b("test_output.txt", std::ios::binary);
        bool same = std::equal(std::istreambuf_iterator<char>(a), std::istreambuf_iterator<char>(),
                                std::istreambuf_iterator<char>(b));
        std::cout << (same ? "MATCH\n" : "MISMATCH\n");

        std::cout << "Files on node:\n";
        for (const auto& id : node.listFiles()) std::cout << "  " << id << "\n";

        std::cout << "Deleting file...\n";
        node.deleteFile(fileId);
        std::cout << "Files remaining: " << node.listFiles().size() << "\n";

    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
}