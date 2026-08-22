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

        // Create 4 small test files - watch them round-robin across 3 nodes
        for (int i = 1; i <= 4; ++i) {
            std::string name = "coord_test_" + std::to_string(i) + ".txt";
            std::ofstream f(name);
            f << "This is test file number " << i << " for the coordinator.";
            f.close();

            std::string fileId = coord.distributeFile(name);

            std::string outName = "coord_out_" + std::to_string(i) + ".txt";
            coord.retrieveFile(fileId, outName);

            std::ifstream a(name, std::ios::binary), b(outName, std::ios::binary);
            bool same = std::equal(std::istreambuf_iterator<char>(a), std::istreambuf_iterator<char>(),
                                    std::istreambuf_iterator<char>(b));
            std::cout << "File " << i << ": " << (same ? "MATCH" : "MISMATCH") << "\n\n";
        }
    } catch (std::exception& e) {
        std::cerr << "Coordinator exception: " << e.what() << "\n";
    }
}