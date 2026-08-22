#include "coordinator.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>

int main() {
    try {
        dfs::Coordinator coord({
            {"127.0.0.1", 6001},
            {"127.0.0.1", 6002},
            {"127.0.0.1", 6003}
        });

        const std::string inputFile = "big_test.txt";
        {
            std::ofstream f(inputFile);
            for (int i = 0; i < 100000; ++i) f << "Chunk distribution and replication test data. ";
        }

        std::string fileId = coord.distributeFile(inputFile);
        std::cout << "\nDistributed fileId: " << fileId << "\n\n";

        coord.retrieveFile(fileId, "big_test_output.txt");

        std::ifstream a(inputFile, std::ios::binary), b("big_test_output.txt", std::ios::binary);
        bool same = std::equal(std::istreambuf_iterator<char>(a), std::istreambuf_iterator<char>(),
                                std::istreambuf_iterator<char>(b));
        std::cout << "\n" << (same ? "MATCH - chunk-level distributed retrieval successful!"
                                    : "MISMATCH!") << "\n";
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }
}